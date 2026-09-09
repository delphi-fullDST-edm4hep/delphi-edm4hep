#!/usr/bin/env python3
"""Check runquality.py against SKELANA's own selection.

psrunq_probe dumps the runs PSRUNQ accepts for a given window, as ranges closed
on the row before a rejection. This expands them in file order and requires
runquality.py to select exactly the same (run, fileSeq) pairs.

    crosscheck.py [reference.txt]

Defaults to psrunq_iflrnq.txt beside this script. Exits 0 on an exact match.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import runquality as rq

HERE = os.path.dirname(os.path.abspath(__file__))


def reference(path):
    """The window and the accepted ranges the probe recorded."""
    window, ranges = {}, []
    for line in open(path):
        if line.startswith("# window:"):
            for item in line.split(":", 1)[1].split():
                name, _, level = item.partition(">=")
                window[name] = int(level)
        elif not line.startswith("#") and line.strip():
            first_run, first_seq, last_run, last_seq = map(int, line.split())
            ranges.append(((first_run, first_seq), (last_run, last_seq)))
    return window, ranges


def expand(ranges, ordered):
    """The ranges as a set of keys, walked in the order PSRUNQ read the rows.

    A range closes on the last accepted row, so it cannot be expanded by
    comparing run numbers: the 1994 file is not sorted.
    """
    out, index, inside = set(), 0, False
    for key in ordered:
        if not inside and index < len(ranges) and key == ranges[index][0]:
            inside = True
        if inside:
            out.add(key)
            if key == ranges[index][1]:
                inside, index = False, index + 1
    return out, index


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        HERE, "psrunq_iflrnq.txt")
    window, ranges = reference(path)
    print(f"reference {os.path.basename(path)}: {len(ranges)} ranges, window "
          + " ".join(f"{n}>={v}" for n, v in sorted(window.items())))

    # PSRUNQ reads every year in FILNAM order and accumulates, so the
    # comparison has to span them all in that same order.
    ordered, ours = [], set()
    for year in rq.RUNQUALI:
        rows = list(rq.rows(year))
        ordered += [(run, seq) for run, seq, _ in rows]
        table = {(run, seq): flags for run, seq, flags in rows}
        ours |= rq.select(table, window)[0]

    theirs, used = expand(ranges, ordered)
    if used != len(ranges):
        print(f"only {used} of {len(ranges)} ranges matched a row: the files "
              f"and the reference disagree", file=sys.stderr)
        return 1

    print(f"rows {len(ordered)}   SKELANA accepts {len(theirs)}   "
          f"runquality.py accepts {len(ours)}")
    missing, extra = theirs - ours, ours - theirs
    if not missing and not extra:
        print("exact match")
        return 0
    for label, keys in (("we reject, SKELANA accepts", missing),
                        ("we accept, SKELANA rejects", extra)):
        if keys:
            print(f"{label}: {len(keys)}  e.g. {sorted(keys)[:6]}",
                  file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
