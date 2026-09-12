#!/usr/bin/env python3
"""Select DELPHI runs by detector quality, and sum their luminosity.

DELPHI recorded the state of every subdetector and trigger, run by run, in the
RUNQUALI.SUMARY<yy> files, and the delivered luminosity in the SATLUM/STILUM
files. Both are keyed by (run, file-within-run) -- the same key converted files
carry as the frame parameters EVT_runNumber and EVT_fileSeq.

There is no single good-run list. Which detectors a measurement depends on
decides which runs it can use, so the requirements are the caller's to state.
SKELANA's own choice, applied when IFLRNQ > 0, is available as --preset iflrnq.

Quality flags run 0-9: 0 unusable, 5 90-95% nominal, 6 95-99%, 7 above 99%,
8 varying during the run, 9 unknown. Flags 8 and 9 exceed 7 numerically but
are not better than it; --strict rejects them.

Usage:
    runquality.py --year 94 --flags
    runquality.py --year 94 --require TPC=7 VD=1 OD=6 MUB=5
    runquality.py --year 94 --preset iflrnq --out good.json
"""
import argparse
import collections
import glob
import json
import os
import sys

# Filenames per year, transcribed from PSRUNQ's FILNAM table (skelana.car).
RUNQUALI = {
    "90": "RUNQUALI.SUMARY90", "91": "RUNQUALI.SUMARY91",
    "92": "RUNQUALI.SUMARY92", "93": "RUNQUALI.SUMARY93",
    "94": "RUNQUALI.SUMARY94", "95": "RUNQUALI.SUMARY95",
    # The LEP1.5 run at the end of 1995, 130-136 GeV: its own file in FILNAM,
    # covering runs 63905-64733, disjoint from SUMARY95. No luminosity file
    # here covers it.
    "95P3": "RUNQUALI.SUMARY95P3",
    "96": "RUNQUALI.SUMARY96", "97": "RUNQUALI.SUMARY97",
    "98": "RUNQUALI.SUMARY98", "99": "RUNQUALI.SUMARY99",
    "00": "RUNQUALI.SUMARY00",
}

# SUMAR93C and SUMAR95B also sit in the data directory but are superseded
# alternates, absent from FILNAM, and so not read by SKELANA either.

# Luminosity came from the SAT through 1993 and the STIC from 1994. A year can
# offer several sets: LEP2 split by energy point (_130, _183 GeV) and running
# period (_P1, _P2, _Z0), and 1993 has both SAT and LUMI families. Those are
# different physics programmes and must not be summed together, so where there
# is more than one the caller picks with --era. 1990 and 1991 have none.
LUMI = {
    "92": ("SATLUM92",),
    "93": ("SATLUM93", "LUMI93", "LUMI93FV", "LUMI93RV", "LUMI93_SJAN96"),
    "94": ("STILUM94",),
    "95": ("STILUM95",),
    "96": ("STILUM96", "STILUM96_P1", "STILUM96_P2", "STILUM96_Z0"),
    "97": ("STILUM97_130", "STILUM97_183", "STILUM97_P1", "STILUM97_Z0"),
    "98": ("STILUM98", "STILUM98_Z0"),
    "99": ("STILUM99",),
    "00": ("STILUM00",),
}

# Detector names and order are DETNAM from skelana.car. The 1994 files fill
# only the first 31; the rest were added for the later years.
DETECTORS = (
    "MVX_A", "MVX_C", "ID_JET", "ID_TRIG", "TPC_0", "TPC_1", "BRICH_L",
    "BRICH_G", "OD_B", "OD_D", "HPC_0", "HPC_1", "HCAB_A", "HCAB_C", "MUB_B",
    "MUB_D", "FCA_A", "FCA_C", "RIF_A", "RIF_C", "FCB_A", "FCB_C", "EMF_A",
    "EMF_C", "HCAF_A", "HCAF_C", "MUF_A", "MUF_C", "SAT_CAL", "SAT_TRA",
    "VSAT", "VFT_PIX", "VFT_STR", "MUS", "TOF", "TAG_40", "TAG_90", "TAG_PHI",
)

# Both halves or sides of a detector, so a caller writes TPC rather than
# TPC_0 and TPC_1. Either spelling is accepted.
GROUPS = {
    "VD": ("MVX_A", "MVX_C"), "ID": ("ID_JET", "ID_TRIG"),
    "TPC": ("TPC_0", "TPC_1"), "BRICH": ("BRICH_L", "BRICH_G"),
    "OD": ("OD_B", "OD_D"), "HPC": ("HPC_0", "HPC_1"),
    "HCAB": ("HCAB_A", "HCAB_C"), "MUB": ("MUB_B", "MUB_D"),
    "FCA": ("FCA_A", "FCA_C"), "RIF": ("RIF_A", "RIF_C"),
    "FCB": ("FCB_A", "FCB_C"), "EMF": ("EMF_A", "EMF_C"),
    "HCAF": ("HCAF_A", "HCAF_C"), "MUF": ("MUF_A", "MUF_C"),
    "SAT": ("SAT_CAL", "SAT_TRA"), "VFT": ("VFT_PIX", "VFT_STR"),
}

# SKELANA's own requirements, set in USER00 when IFLRNQ > 0. A floor, not a
# physics selection: the DELPHI note says to choose your own.
PRESETS = {"iflrnq": {"MVX_A": 1, "MVX_C": 1, "TPC_0": 7, "TPC_1": 7}}

# Column layout and the file-code table are PSRUNQ's own. A whitespace parse
# silently mis-reads the two-letter codes, which carry no separator: the row
# for run 51016 segment AA begins "51016AA0".
COLUMNS = ((9, 19), (20, 26), (27, 39), (40, 43))
FILES = ("A B C D E F G H I J K L M N O P Q R S T U "
         "V W X Y Z AAABACADAEAFAGAHAIAJAKALAMANAOAP"
         "AQARASATAUAVAWAXAYAZBABCBDBEBFBGBHBIBJBKBL")

def named(year):
    """The year label as a reader expects it: 94 -> 1994, 00 -> 2000."""
    return f"19{year}" if year[:2] != "00" else "2000"


MONTHS = ("JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP",
          "OCT", "NOV", "DEC")


def _dat(override=None):
    """The DELPHI data directory, from the environment unless overridden."""
    path = override or os.environ.get("DELPHI_DAT")
    if not path:
        sys.exit("DELPHI_DAT is not set: source /cvmfs/delphi.cern.ch/setup.sh"
                 " or pass --dat")
    return path


def _fileseq(code):
    """PSRUNQ's NFIL: the two-character segment code as a file number."""
    at = FILES.find(code)
    return max((at + 2) // 2, 1) if at >= 0 else 1


def _stamp(path):
    """Sort key for the DDMMMYY suffix these files carry.

    The suffix is a date, not a sortable string: 03FEB98 is newer than 13JAN97
    but sorts before it. One 1997 set is written 021097 instead; those return
    None and are never picked automatically, only by name.
    """
    tail = path.rsplit(".", 1)[-1]
    if len(tail) != 7 or not tail[:2].isdigit() or tail[2:5] not in MONTHS:
        return None
    year = int(tail[5:])
    return (year + (1900 if year >= 90 else 2000),
            MONTHS.index(tail[2:5]) + 1, int(tail[:2]))


def rows(year, dat=None):
    """Every row of a year's file, in the order PSRUNQ reads them.

    Rows come in three formats. PSRUNQ picks between them using the fill number
    that heads each block of runs:

        fill > 5450     run is 6 digits, file a 2-digit number
        run  > 72000    run is 5 digits, file a 2-digit number
        otherwise       run is 5 digits, file a 2-letter segment code

    Runs above 72000 carry seven further detectors, in columns 45-51.

    File order matters and is not sorted order: SKELANA closes a range of
    accepted runs on the row before a rejection, and in the 1994 file run
    45006 follows run 45219.
    """
    path = os.path.join(_dat(dat), RUNQUALI[year])
    fill = 0
    for line in open(path, errors="replace"):
        line = line.rstrip("\n").ljust(60)

        # Each block of runs is headed by its fill, and the fill picks the format.
        if line[1:5] == "FILL":
            if line[5:9].strip().isdigit():
                fill = int(line[5:9])
            continue

        wide = fill > 5450                   # LEP2 numbered its runs in six digits
        head = line[:6] if wide else line[:5]
        if not head.strip().isdigit():
            continue                         # legend, banner or blank line
        run = int(head)

        code = line[6:8] if wide else line[5:7]
        if wide or run > 72000:
            seq = int(code) if code.strip().isdigit() else 1   # already a number
        else:
            seq = _fileseq(code)             # segment letter: 'A' -> 1, 'AA' -> 27

        flags = "".join(line[a:b] for a, b in COLUMNS)
        if run > 72000:
            flags += line[44:51]             # VFT, MUS, TOF and the taggers
        if not flags[:31].isdigit():
            continue                         # not a data row after all
        yield run, seq, flags


def load(year, dat=None):
    """(run, fileSeq) -> per-detector quality flags, as a string of digits."""
    return {(run, seq): flags for run, seq, flags in rows(year, dat)}


def luminosity(year, dat=None, era=None, version=None):
    """(run, fileSeq) -> nb-1 delivered, and the file it came from.

    Rows carry a per-fill systematic common to many rows, so the errors are not
    summed here; only the delivered luminosity is.
    """
    sets = LUMI.get(year, ())
    if not sets:
        return {}, None
    if era:
        sets = tuple(s for s in sets if s == era)
        if not sets:
            sys.exit(f"unknown era for {named(year)}: {', '.join(LUMI[year])}")
    if len(sets) > 1:
        sys.exit(f"{named(year)} covers several energy points or periods, which"
                 f" must not be summed together.\nPick one with --era: "
                 f"{', '.join(LUMI[year])}")
    # The version separator is '.', the family separator '_', so this matches
    # STILUM96.* without also matching STILUM96_P1.*
    found = glob.glob(os.path.join(_dat(dat), sets[0] + ".*"))
    if version:
        found = [f for f in found if f.rsplit(".", 1)[-1] == version]
        if not found:
            sys.exit(f"no {sets[0]} file with version {version}")
        path = found[0]
    else:
        dated = [(_stamp(f), f) for f in found if _stamp(f)]
        if not dated:
            sys.exit(f"cannot date the {sets[0]} files; pass --lumi-version")
        path = max(dated)[1]
    table = collections.defaultdict(float)
    for line in open(path, errors="replace"):
        part = line.split()
        if len(part) < 9 or not part[0].isdigit():
            continue
        try:
            value = float(part[7])
        except ValueError:
            continue
        # Several rows can share one (run, file): 1995 splits most of its keys
        # across three. They add.
        table[(int(part[0]), int(part[1]))] += value
    return dict(table), os.path.basename(path)


def requirements(items, preset=None):
    """DET=flag arguments, and any preset, as {detector: minimum flag}."""
    want = dict(PRESETS[preset]) if preset else {}
    for item in items or []:
        name, _, level = item.partition("=")
        name = name.upper()
        if not level.isdigit():
            sys.exit(f"bad requirement {item!r}, expected e.g. TPC=7")
        for one in GROUPS.get(name, (name,)):
            if one not in DETECTORS:
                sys.exit(f"unknown detector {name!r}")
            want[one] = int(level)
    return want


def achievable(table):
    """Per detector: the best flag seen, the share of rows reaching 5, 6 and 7,
    and the share that is 8 or 9 -- what a threshold would actually cost."""
    out, rows = {}, len(table) or 1
    for i, name in enumerate(DETECTORS):
        seen = [f[i] for f in table.values() if len(f) > i and f[i].isdigit()]
        if not seen:
            continue
        count = collections.Counter(int(f) for f in seen)
        out[name] = {
            "max": max(count),
            # Counted over known, settled flags only: 8 and 9 are reported
            # separately rather than inflating the "at least 7" share.
            "at": {level: sum(n for f, n in count.items() if level <= f <= 7)
                   / rows for level in (5, 6, 7)},
            "unsettled": sum(n for f, n in count.items() if f >= 8) / rows,
        }
    return out


def select(table, want, strict=False):
    """Accepted keys, and how many rows each requirement rejected."""
    good, rejected = set(), collections.Counter()
    for key, flags in table.items():
        ok = True
        for name, level in want.items():
            index = DETECTORS.index(name)
            # Files before 1999 carry only the first 31 detectors. PSRUNQ
            # leaves the rest at the zero PSINI set, so they fail any
            # requirement above 0 rather than being absent.
            flag = flags[index] if index < len(flags) else "0"
            value = int(flag) if flag.isdigit() else 9
            if value < level or (strict and value > 7):
                rejected[name] += 1
                ok = False
        if ok:
            good.add(key)
    return good, rejected


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--year", required=True,
                        help="two-digit year, e.g. 94, 00; also 95P3 (LEP1.5)")
    parser.add_argument("--dat", help="override $DELPHI_DAT")
    parser.add_argument("--require", nargs="*", metavar="DET=FLAG",
                        help="minimum quality, e.g. TPC=7 OD=6 MUB=5")
    parser.add_argument("--preset", choices=sorted(PRESETS))
    parser.add_argument("--strict", action="store_true",
                        help="reject flags 8 (varied) and 9 (unknown)")
    parser.add_argument("--flags", action="store_true",
                        help="what each threshold would cost, then stop")
    parser.add_argument("--era", help="energy point or period, where a year "
                                      "has several; also restricts the runs")
    parser.add_argument("--lumi-version", help="pin a dated recalibration")
    parser.add_argument("--no-lumi", action="store_true",
                        help="skip luminosity entirely")
    parser.add_argument("--out", help="write the selection as JSON")
    args = parser.parse_args()

    args.year = args.year.upper()
    if args.year not in RUNQUALI:
        sys.exit(f"no run-quality file for {args.year!r};"
                 f" have {', '.join(RUNQUALI)}")
    table = load(args.year, args.dat)
    reach = achievable(table)
    print(f"{named(args.year)}: {len(table)} (run, fileSeq) entries",
          flush=True)

    if args.flags:
        print(f'\n{"detector":9s} {"max":>4} {">=5":>8} {">=6":>8} {">=7":>8}'
              f' {"8 or 9":>8}')
        for name, info in reach.items():
            share = info["at"]
            print(f'{name:9s} {info["max"]:>4} '
                  + " ".join(f'{100*share[l]:7.1f}%' for l in (5, 6, 7))
                  + f' {100*info["unsettled"]:7.1f}%')
        return 0

    want = requirements(args.require, args.preset)
    if not want:
        sys.exit("nothing required: pass --require or --preset")

    # An unreachable threshold is the common mistake, and an empty selection
    # does not explain itself.
    for name, level in sorted(want.items()):
        best = reach.get(name, {}).get("max")
        if best is not None and best < level:
            print(f"warning: {name} never exceeds {best} in {named(args.year)},"
                  f" so {name}>={level} can never be satisfied", file=sys.stderr)

    good, rejected = select(table, want, args.strict)

    lumi, source = ({}, None) if args.no_lumi else \
        luminosity(args.year, args.dat, args.era, args.lumi_version)
    # An era is a distinct physics programme, so naming one also restricts the
    # runs to it. Years with a single set are left alone: a measurement that
    # needs no luminosity should not lose runs that merely lack a record.
    if args.era and lumi:
        before = len(good)
        good &= set(lumi)
        print(f"era {args.era}: kept {len(good)} of {before} selected")

    print(f"selected {len(good)} of {len(table)}"
          f"  ({100*len(good)/max(len(table),1):.1f}%)")
    if lumi:
        covered = [k for k in good if k in lumi]
        print(f"luminosity {sum(lumi[k] for k in covered)/1000:.2f} pb-1 over "
              f"{len(covered)} of them, from {sum(lumi.values())/1000:.2f} pb-1"
              f" available   [{source}]")
        if len(covered) < len(good):
            print(f"           {len(good)-len(covered)} selected keys have no"
                  f" luminosity record")
    if rejected:
        print("rejected by: "
              + ", ".join(f"{n} {c}" for n, c in rejected.most_common(8)))

    if args.out:
        with open(args.out, "w") as out:
            json.dump({"year": args.year, "requirements": want,
                       "strict": args.strict, "era": args.era,
                       "luminosity_file": source,
                       "selected": sorted(good)}, out, indent=1)
        print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
