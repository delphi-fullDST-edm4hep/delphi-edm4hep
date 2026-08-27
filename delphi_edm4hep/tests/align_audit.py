#!/usr/bin/env python3
"""Alignment / relation audit for delphi_edm4hep EDM4hep output.

For every UserDataCollection the converter emits, the README documents which
object collection it runs parallel to (index-aligned). This scanner checks
those length contracts on a real EDM4hep file, plus the RecDqdx -> Track
relation. It is the regression guard for the bug class where a per-particle
array is mislabelled as per-track (so consumers indexing by track read the
wrong row), or a relation is silently left unset -- neither of which the
momentum/value-level checks (thrust, "dE/dx non-zero") can see. The failure is
only visible on events that contain a reco neutral (Particles > Tracks), e.g.
Z->mumu(gamma): a clean 2-track event has Particles==Tracks and hides it.

Usage:
    align_audit.py <file.edm4hep.root>
    DELPHI_EDM4HEP_SAMPLE=<file.edm4hep.root> align_audit.py

Exit 0 if all contracts hold, 77 if no input is configured (CTest skip), and
1 if an explicitly configured input is missing or any contract fails.
"""
import os
import sys

# README parallelism contracts: UserData collection -> collection it must
# match in length, event by event.
LENGTH_CONTRACTS = {
    "sDST_TRAC_d0PV":              "sDST_TRAC_Tracks",
    "sDST_TRAC_z0PV":              "sDST_TRAC_Tracks",
    "sDST_TRAC_d0BS":              "sDST_TRAC_Tracks",
    "sDST_VECP_LVLOCK":            "sDST_MAIN_Particles",
    "sDST_PV_Vertices_StatusBits": "sDST_PV_Vertices",
    "sDST_TDVD_VDHits_TrackIndex": "sDST_TDVD_VDHits",
    "sDST_ELTR_ParticleIndex":     "sDST_ELTR_RefitTracks",
    "fDST_MAIN_MatchProvenance":   "fDST_MAIN_Particles",
}

AABTAG_TRACK_SUFFIXES = (
    "ParticleIndex",
    "ImpactParRPhi",
    "ImpactParRPhiError",
    "ImpactParZ",
    "ImpactParZError",
    "ProbRPhi",
    "ProbZ",
    "UsedForTag",
    "AttachedToPV",
    "NVDHitsRPhi",
    "NVDHitsZ",
    "NVDLayersRPhi",
    "NVDLayersZ",
    "Chi2VD",
    "Chi2PV",
    "Momentum",
)


def main():
    fn = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] \
        else os.environ.get("DELPHI_EDM4HEP_SAMPLE", "")
    if not fn:
        print("align_audit: no input (pass a file or set DELPHI_EDM4HEP_SAMPLE)"
              " -> skipping")
        return 77
    if not os.path.exists(fn):
        print(f"align_audit: configured input not found: {fn}")
        return 1

    from podio import root_io
    reader = root_io.Reader(fn)

    len_fail = {}   # (userdata, parallel) -> [(event, got, expected), ...]
    dq_missing = dq_total = nev = 0
    dummy_first = dummy_published_as_primary = dummy_chain_marked_primary = 0
    fdst_btag_tracks = fdst_btag_resolved = fdst_btag_bad_index = 0
    aabtag_events = aabtag_rows = 0
    aabtag_missing = {}
    aabtag_len_fail = {}
    for i, fr in enumerate(reader.get("events")):
        nev += 1
        names = set(fr.getAvailableCollections())
        size = lambda n: fr.get(n).size() if n in names else None
        for ud, par in LENGTH_CONTRACTS.items():
            a, b = size(ud), size(par)
            if a is not None and b is not None and a != b:
                len_fail.setdefault((ud, par), []).append((i, a, b))
        for n in names:
            if n.endswith("_RecDqdx"):
                for dq in fr.get(n):
                    dq_total += 1
                    if dq.getTrack().getObjectID().index < 0:
                        dq_missing += 1
        status_name = "sDST_PV_Vertices_StatusBits"
        primary_name = "sDST_PV_PrimaryVertex"
        if status_name in names and primary_name in names:
            statuses = fr.get(status_name)
            if statuses.size() and (int(statuses[0]) & 0x01):
                dummy_first += 1
                if fr.get(primary_name).size():
                    dummy_published_as_primary += 1
                vertices = fr.get("sDST_PV_Vertices")
                if vertices.size() and vertices[0].isPrimary():
                    dummy_chain_marked_primary += 1
        index_name = "fDST_AABTAG_Tracks_ParticleIndex"
        particles_name = "fDST_MAIN_Particles"
        if index_name in names and particles_name in names:
            particle_count = fr.get(particles_name).size()
            for raw_index in fr.get(index_name):
                index = int(raw_index)
                fdst_btag_tracks += 1
                if 0 <= index < particle_count:
                    fdst_btag_resolved += 1
                elif index != -1:
                    fdst_btag_bad_index += 1
        for source in ("sDST", "fDST"):
            prefix = f"{source}_AABTAG_Tracks_"
            # The AABTAG PV collection is emitted on every recalculated frame,
            # even when it is empty for a bad event. Use it as the schema
            # anchor so wholesale loss of all 16 parallel arrays cannot pass
            # merely because there is no array left to discover.
            if f"{source}_AABTAG_PrimaryVertex" not in names:
                continue
            present = [suffix for suffix in AABTAG_TRACK_SUFFIXES
                       if prefix + suffix in names]
            aabtag_events += 1
            missing = [suffix for suffix in AABTAG_TRACK_SUFFIXES
                       if suffix not in present]
            for suffix in missing:
                key = prefix + suffix
                aabtag_missing[key] = aabtag_missing.get(key, 0) + 1
            sizes = {suffix: size(prefix + suffix) for suffix in present}
            expected = sizes.get("ParticleIndex")
            if expected is None:
                continue
            aabtag_rows += expected
            for suffix, actual in sizes.items():
                if actual != expected:
                    key = (prefix + suffix, prefix + "ParticleIndex")
                    aabtag_len_fail.setdefault(key, []).append(
                        (i, actual, expected))

    print(f"align_audit: {fn}  ({nev} events)")
    ok = True
    if nev == 0:
        ok = False
        print("  FAIL  input contains no event frames")
    if len_fail:
        ok = False
        for (ud, par), lst in sorted(len_fail.items()):
            e = lst[0]
            print(f"  FAIL  len({ud}) != len({par}) in {len(lst)}/{nev} events "
                  f"(e.g. evt{e[0]}: {e[1]} vs {e[2]})")
    else:
        print(f"  OK    all {len(LENGTH_CONTRACTS)} length contracts hold")
    if dq_missing:
        ok = False
        print(f"  FAIL  {dq_missing}/{dq_total} RecDqdx rows missing a Track link")
    elif dq_total:
        print(f"  OK    all {dq_total} RecDqdx rows carry a Track link")
    if dummy_published_as_primary:
        ok = False
        print(f"  FAIL  {dummy_published_as_primary}/{dummy_first} events with "
              "a dummy first DELPHI vertex published sDST_PV_PrimaryVertex")
    elif dummy_first:
        print(f"  OK    all {dummy_first} dummy first DELPHI vertices are "
              "excluded from sDST_PV_PrimaryVertex")
    if dummy_chain_marked_primary:
        ok = False
        print(f"  FAIL  {dummy_chain_marked_primary}/{dummy_first} dummy first "
              "DELPHI vertices remain marked primary in sDST_PV_Vertices")
    elif dummy_first:
        print(f"  OK    all {dummy_first} dummy first DELPHI vertices are marked "
              "non-primary in sDST_PV_Vertices")
    if fdst_btag_bad_index:
        ok = False
        print(f"  FAIL  {fdst_btag_bad_index}/{fdst_btag_tracks} fDST AABTAG "
              "particle indices are outside [-1, len(fDST_MAIN_Particles))")
    if fdst_btag_tracks and not fdst_btag_resolved:
        ok = False
        print(f"  FAIL  all {fdst_btag_tracks} fDST AABTAG particle indices are "
              "unresolved (-1)")
    elif fdst_btag_tracks:
        print(f"  OK    {fdst_btag_resolved}/{fdst_btag_tracks} fDST AABTAG "
              "particle indices resolve into fDST_MAIN_Particles")
    if aabtag_missing:
        ok = False
        for name, count in sorted(aabtag_missing.items()):
            print(f"  FAIL  {name} missing in {count} AABTAG event(s)")
    if aabtag_len_fail:
        ok = False
        for (ud, parallel), failures in sorted(aabtag_len_fail.items()):
            event, got, expected = failures[0]
            print(f"  FAIL  len({ud}) != len({parallel}) in "
                  f"{len(failures)} event(s) (e.g. evt{event}: "
                  f"{got} vs {expected})")
    elif aabtag_events and not aabtag_missing:
        print(f"  OK    all {len(AABTAG_TRACK_SUFFIXES)} AABTAG track arrays "
              f"are mutually aligned in {aabtag_events} event(s), "
              f"covering {aabtag_rows} rows")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
