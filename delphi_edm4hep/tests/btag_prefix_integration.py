#!/usr/bin/env python3
"""Optional two-prefix integration check over a real pass-2 EDM4hep file."""

from __future__ import annotations

import os
import shlex
import subprocess
import sys
from pathlib import Path


def parse_summary(stdout: str) -> dict[str, str]:
    lines = [line for line in stdout.splitlines() if line.startswith("status=")]
    if len(lines) != 1:
        raise RuntimeError(f"expected one checker summary, got {len(lines)}")
    fields: dict[str, str] = {}
    for token in shlex.split(lines[0]):
        key, sep, value = token.partition("=")
        if not sep:
            raise RuntimeError(f"malformed summary token: {token!r}")
        fields[key] = value
    return fields


def main() -> int:
    sample_text = os.environ.get("DELPHI_EDM4HEP_TWOPASS_SAMPLE")
    if not sample_text:
        print("SKIP: DELPHI_EDM4HEP_TWOPASS_SAMPLE is not set")
        return 77
    sample = Path(sample_text)
    if not sample.is_file():
        raise RuntimeError(f"sample is not a regular file: {sample}")

    checker = Path(sys.argv[1])
    expected_run = os.environ.get("DELPHI_EDM4HEP_EXPECTED_RUN", "data")
    mode = os.environ.get("DELPHI_EDM4HEP_BTAG_MODE", "recalc")
    primary_vertex_policy = os.environ.get(
        "DELPHI_EDM4HEP_PRIMARY_VERTEX_POLICY", "keep-delana"
    )
    for source in ("sDST", "fDST"):
        command = [
            str(checker),
            "--source",
            source,
            "--primary-vertex-policy",
            primary_vertex_policy,
            str(sample),
            expected_run,
            mode,
        ]
        result = subprocess.run(command, text=True, capture_output=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(
                f"{source} check failed ({result.returncode}):\n"
                f"{result.stdout}{result.stderr}"
            )
        fields = parse_summary(result.stdout)
        required = {
            "status": "PASS",
            "source": source,
            "identity_source": "sDST_EVT",
            "provenance_schema": "source-local-v1",
            "beamspot_status_source": f"{source}_BTAGCFG_BeamSpotErrorCode",
            "primary_vertex_policy": primary_vertex_policy,
            "primary_vertex_policy_expected": primary_vertex_policy,
            "iflpvt": "0" if primary_vertex_policy == "keep-delana" else "1",
            "primary_vertex_policy_failures": "0",
            "legacy_fdst_beamspot_fallback_used": "0",
            "off_payload_presence_failures": "0",
            "bank_payload_empty_failures": "0",
            "track_impact_nonfinite_failures": "0",
            "track_error_domain_failures": "0",
            "track_chi2_domain_failures": "0",
            "track_momentum_domain_failures": "0",
            "track_integer_domain_failures": "0",
            "pv_primary_flag_failures": "0",
            "pv_algorithm_type_failures": "0",
            "pv_nonfinite_failures": "0",
            "pv_ndf_domain_failures": "0",
            "failures": "0",
        }
        for key, value in required.items():
            if fields.get(key) != value:
                raise RuntimeError(
                    f"{source} summary has {key}={fields.get(key)!r}, expected {value!r}"
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
