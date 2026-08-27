#!/usr/bin/env python3
"""Exercise a throwing event hook without unwinding through PHDST/Fortran."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: harness_exception_containment.py <probe-executable>", file=sys.stderr)
        return 64

    sample_text = os.environ.get("DELPHI_PHDST_SAMPLE", "")
    if not sample_text:
        print("SKIP: DELPHI_PHDST_SAMPLE is not set")
        return 77

    sample = Path(sample_text).resolve()
    if not sample.is_file():
        print(f"FAIL: DELPHI_PHDST_SAMPLE is not a file: {sample}", file=sys.stderr)
        return 1

    probe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="harness-exception-") as tmp_text:
        tmp = Path(tmp_text)
        result = subprocess.run(
            [str(probe), str(sample), str(tmp / "partial.root")],
            cwd=tmp,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    combined = result.stdout + result.stderr
    failures: list[str] = []
    if result.returncode != 3:
        failures.append(f"expected exit 3, got {result.returncode}")
    if "caught C++ user02 event callback failure: injected event failure" not in combined:
        failures.append("missing caught-event diagnostic")
    if "harness::run: event callback failed: user02 event callback: injected event failure" not in combined:
        failures.append("missing run-level failure diagnostic")
    if "harness_exception_probe: event_calls=1" not in combined:
        failures.append("event hook did not run exactly once")
    if "partial output is not publishable" not in combined:
        failures.append("missing partial-output diagnostic")
    if re.search(r"delphi_edm4hep::harness: wrote\s+\d+\s+events to", combined):
        failures.append("failed job emitted the canonical success footer")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        print("--- probe stdout ---", file=sys.stderr)
        print(result.stdout, file=sys.stderr)
        print("--- probe stderr ---", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return 1

    print("PASS: throwing event callback stopped cleanly at the C++/Fortran boundary")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
