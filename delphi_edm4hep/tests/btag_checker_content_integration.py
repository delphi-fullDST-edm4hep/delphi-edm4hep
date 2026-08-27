#!/usr/bin/env python3
"""Focused malformed-content checks for delphi_btag_check."""

from __future__ import annotations

import math
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import ROOT
    import edm4hep
    from podio import Frame, root_io
except ImportError:
    print("SKIP: Python podio/EDM4hep bindings are unavailable")
    raise SystemExit(77)


def parse_summary(stdout: str) -> dict[str, str]:
    lines = [line for line in stdout.splitlines() if line.startswith("status=")]
    if len(lines) != 1:
        raise RuntimeError(f"expected one checker summary, got {len(lines)}")
    fields: dict[str, str] = {}
    for token in shlex.split(lines[0]):
        key, separator, value = token.partition("=")
        if not separator or key in fields:
            raise RuntimeError(f"malformed/duplicate summary token: {token!r}")
        fields[key] = value
    return fields


def common_frame(mode: str, recalculated: int) -> Frame:
    frame = Frame()
    for key, value in {
        "sDST_EVT_runNumber": 1,
        "sDST_EVT_eventNumber": 2,
        "sDST_EVT_fileSeq": 3,
        "sDST_BTAGCFG_Mode": mode,
        "sDST_BTAGCFG_Recalculated": recalculated,
        "sDST_BTAGCFG_BeamSpotErrorCode": 0,
        "sDST_BTAGCFG_SourcePrefix": "sDST",
        "sDST_BTAGCFG_PrimaryVertexPolicy": "keep-delana",
        "sDST_BTAGCFG_IFLPVT": 0,
    }.items():
        frame.put_parameter(key, value)
    return frame


def put_float(frame: Frame, key: str, value: float) -> None:
    frame.put_parameter(key, value, as_type="float")


def put_float_vector(frame: Frame, key: str, values: list[float]) -> None:
    frame.put_parameter(key, values, as_type="float")


def put_userdata(frame: Frame, key: str, cpp_type: str, values: list) -> None:
    collection = ROOT.podio.UserDataCollection[cpp_type]()
    for value in values:
        collection.push_back(value)
    frame.put(collection, key)


def bank_frame(finite_word: bool) -> Frame:
    frame = common_frame("bank", 0)
    nan = math.nan
    first = 0.5 if finite_word else nan
    put_float_vector(frame, "sDST_BTG_ProbNegIP", [first, nan, nan])
    put_float_vector(frame, "sDST_BTG_ProbPosIP", [nan, nan, nan])
    put_float_vector(frame, "sDST_BTG_ProbAllIP", [nan, nan, nan])
    put_float_vector(frame, "sDST_BTG_ThrustAxis", [nan, nan, nan])
    put_float(frame, "sDST_BTG_ThrustValue", nan)
    return frame


def malformed_recalc_frame() -> Frame:
    frame = common_frame("recalc", 1)
    prefix = "sDST_AABTAG_"
    for suffix, value in {
        "BadEventCode": 0,
        "AlgorithmInvoked": 1,
        "Valid": 1,
        "NTracksRaw": 1,
        "NTracks": 1,
        "NTracksAttached": 0,
        "Truncated": 0,
    }.items():
        frame.put_parameter(prefix + suffix, value)
    for suffix in ("ProbNegIP", "ProbPosIP", "ProbAllIP"):
        put_float_vector(frame, prefix + suffix, [0.1, 0.2, 0.3])
    put_float_vector(frame, prefix + "ThrustAxis", [1.0, 0.0, 0.0])
    put_float(frame, prefix + "ThrustValue", 0.8)

    vertex_collection = edm4hep.VertexCollection()
    vertex = vertex_collection.create()
    vertex.setPrimary(False)
    vertex.setAlgorithmType(2)
    vertex.setPosition((math.nan, 0.0, 0.0))
    vertex.setCovMatrix((math.nan, 0.0, 0.0, 0.0, 0.0, 0.0))
    vertex.setChi2(math.nan)
    vertex.setNdf(-1)
    frame.put(vertex_collection, prefix + "PrimaryVertex")
    frame.put(edm4hep.ReconstructedParticleCollection(), "sDST_MAIN_Particles")

    for suffix, values in {
        "ParticleIndex": [-1],
        "UsedForTag": [-1],
        "AttachedToPV": [2],
        "NVDHitsRPhi": [7],
        "NVDHitsZ": [-7],
        "NVDLayersRPhi": [4],
        "NVDLayersZ": [-4],
    }.items():
        put_userdata(frame, prefix + "Tracks_" + suffix, "int32_t", values)
    for suffix, values in {
        "ImpactParRPhi": [math.nan],
        "ImpactParRPhiError": [0.0],
        "ImpactParZ": [math.inf],
        "ImpactParZError": [-1.0],
        "ProbRPhi": [0.5],
        "ProbZ": [0.5],
        "Chi2VD": [-1.0],
        "Chi2PV": [math.nan],
        "Momentum": [0.0],
    }.items():
        put_userdata(frame, prefix + "Tracks_" + suffix, "float", values)
    return frame


def write_frame(path: Path, frame: Frame) -> None:
    writer = root_io.Writer(path)
    writer.write_frame(frame, "events")
    # The Python wrapper normally finishes at process exit. Tests need to open
    # each file immediately, so finish the wrapped writer explicitly.
    writer._writer.finish()  # pylint: disable=protected-access


def run_checker(checker: Path, path: Path, mode: str) -> tuple[int, dict[str, str]]:
    result = subprocess.run(
        [
            str(checker),
            "--source",
            "sDST",
            "--primary-vertex-policy",
            "keep-delana",
            str(path),
            "data",
            mode,
        ],
        text=True,
        capture_output=True,
        check=False,
    )
    if not result.stdout:
        raise RuntimeError(f"checker emitted no stdout:\n{result.stderr}")
    return result.returncode, parse_summary(result.stdout)


def require(fields: dict[str, str], expected: dict[str, str]) -> None:
    for key, value in expected.items():
        if fields.get(key) != value:
            raise RuntimeError(
                f"summary has {key}={fields.get(key)!r}, expected {value!r}"
            )


def main() -> int:
    checker = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="delphi-btag-check-") as directory:
        root = Path(directory)

        clean_off = root / "clean-off.root"
        write_frame(clean_off, common_frame("off", 0))
        returncode, fields = run_checker(checker, clean_off, "off")
        require(fields, {"status": "PASS", "off_payload_presence_failures": "0"})
        if returncode != 0:
            raise RuntimeError(f"clean off fixture returned {returncode}")

        dirty_off = root / "dirty-off.root"
        frame = common_frame("off", 0)
        frame.put_parameter("sDST_AABTAG_Valid", 1)
        write_frame(dirty_off, frame)
        returncode, fields = run_checker(checker, dirty_off, "off")
        require(fields, {"status": "FAIL", "off_payload_presence_failures": "1"})
        if returncode != 2:
            raise RuntimeError(f"dirty off fixture returned {returncode}")

        empty_bank = root / "empty-bank.root"
        write_frame(empty_bank, bank_frame(False))
        returncode, fields = run_checker(checker, empty_bank, "bank")
        require(
            fields,
            {
                "status": "FAIL",
                "missing_event_parameters": "0",
                "bank_payload_empty_failures": "1",
            },
        )
        if returncode != 2:
            raise RuntimeError(f"empty bank fixture returned {returncode}")

        partial_bank = root / "partial-bank.root"
        write_frame(partial_bank, bank_frame(True))
        returncode, fields = run_checker(checker, partial_bank, "bank")
        require(fields, {"status": "PASS", "bank_payload_empty_failures": "0"})
        if returncode != 0:
            raise RuntimeError(f"partial historical bank fixture returned {returncode}")

        malformed = root / "malformed-recalc.root"
        write_frame(malformed, malformed_recalc_frame())
        returncode, fields = run_checker(checker, malformed, "recalc")
        require(
            fields,
            {
                "status": "FAIL",
                "pv_primary_flag_failures": "1",
                "pv_algorithm_type_failures": "1",
                "pv_nonfinite_failures": "1",
                "pv_ndf_domain_failures": "1",
                "track_impact_nonfinite_failures": "2",
                "track_error_domain_failures": "2",
                "track_chi2_domain_failures": "2",
                "track_momentum_domain_failures": "1",
                "track_integer_domain_failures": "6",
                "validity_contract_failures": "1",
            },
        )
        if returncode != 2:
            raise RuntimeError(f"malformed recalc fixture returned {returncode}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
