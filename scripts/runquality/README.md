# Run quality and luminosity

DELPHI recorded the state of every subdetector and trigger run by run, and the
delivered luminosity separately. `runquality.py` turns a set of subdetector
requirements into the runs that satisfy them, and sums their luminosity.

## The key

Both records are keyed by **(run, file-within-run)**. Long runs were split into
lettered segments whose detector states differ, so a run number alone is not
enough: in 1994, 907 of 1895 runs carry more than one entry.

Converted files carry both halves as the frame parameters `EVT_runNumber` and
`EVT_fileSeq`, so a selection applies directly to the EDM4hep output.

## Quality flags

    0  unusable
    1  under 50% of the detector nominal
    2  50-65%      3  65-80%      4  80-90%
    5  90-95%      6  95-99%      7  above 99%
    8  efficiency varied during the run
    9  status unknown

8 and 9 exceed 7 numerically but are not better than it. `--strict` rejects
them; the default admits them, as SKELANA does.

## There is no single good-run list

Which detectors a measurement depends on decides which runs it can use, so
check what a threshold costs before choosing it:

    $ runquality.py --year 94 --flags

    detector   max      >=5      >=6      >=7   8 or 9
    TPC_0        7    99.4%    99.2%    99.2%     0.0%
    OD_B         7    97.5%    97.5%    68.2%     0.0%
    MUB_B        7    93.0%    79.5%    44.9%     0.0%
    EMF_C        6    95.1%    84.5%     0.0%     0.0%
    SAT_CAL      0     0.0%     0.0%     0.0%     0.0%

Three traps are visible there, all from 1994:

- `SAT_CAL` and `SAT_TRA` are 0 in every row. The SAT was retired after 1993
  and the STIC took over, so any requirement on them selects nothing.
- `EMF_C` never reaches 7, so requiring 7 selects nothing.
- `OD` and `MUB` are the expensive ones. OD costs nothing at 6 and a third of
  the data at 7; MUB at 7 discards more than half.

A threshold that can never be met is reported rather than silently returning
an empty selection.

## Usage

    $ runquality.py --year 94 --require VD=1 ID=6 TPC=7 OD=6 MUB=5

    selected 4121 of 5177  (79.6%)
    luminosity 39.46 pb-1 over 4008 of them, from 46.30 pb-1 available
    rejected by: MUB_D 629, MUB_B 362, ID_TRIG 242, ID_JET 206, OD_B 130, ...

Group names (`TPC`, `VD`, `OD`, `MUB`, ...) cover both halves or sides; single
names (`TPC_0`) also work. `--out` writes the selection as JSON, recording the
requirements and the luminosity file alongside the keys.

`--preset iflrnq` is SKELANA's own requirement, set in USER00 when `IFLRNQ > 0`:
VD >= 1 and TPC >= 7, nothing else. It keeps 98.3% of 1994.

## Luminosity

Measured from small-angle Bhabhas: the SAT through 1993, the STIC from 1994.
1990 and 1991 have no luminosity file here.

LEP2 years are split by energy point (`_130`, `_183` GeV) and running period
(`_P1`, `_P2`, `_Z0`), and 1993 carries both SAT and LUMI families. Those are
different physics programmes and must not be summed together, so such years
require `--era`, which also restricts the runs to that era. Years with a single
set are left alone: a measurement that needs no luminosity should not lose runs
that merely lack a record.

Each set has several dated recalibrations, and the newest is used unless
`--lumi-version` names one. The 1994 sets differ by 0.03%:

    STILUM94.10JUL96   46285.2 nb-1
    STILUM94.13JAN97   46299.1 nb-1
    STILUM94.03FEB98   46301.4 nb-1

The file used is printed and recorded in the JSON. Its suffix is a date, not a
sortable string -- `03FEB98` is newer than `13JAN97` but sorts before it -- so
it is parsed rather than sorted.

**Errors are not summed.** Each row carries a per-fill systematic common to
many rows: adding those in quadrature understates it, adding them linearly
overstates it. Only the delivered luminosity is reported.

The two records do not cover the same runs. In 1994, 274 (run, fileSeq) have
quality but no luminosity, and 22 have luminosity but no quality. Selected keys
with no luminosity record are counted and reported rather than dropped
silently.

## Data files

Read from `$DELPHI_DAT` (`source /cvmfs/delphi.cern.ch/setup.sh`), or `--dat`.
On the current release that resolves to

    /cvmfs/delphi.cern.ch/releases/almalinux-9-x86_64/v24072026-1/dstana/161018/dat

Filenames per year come from PSRUNQ's own FILNAM table. Where a year offers
several luminosity sets they are different energy points or running periods,
and `--era` picks one. The version shown is the newest, used unless
`--lumi-version` names another.

    year  run quality            luminosity set   version used            sqrt(s)   pb-1
    90    RUNQUALI.SUMARY90      (none)
    91    RUNQUALI.SUMARY91      (none)
    92    RUNQUALI.SUMARY92      SATLUM92         SATLUM92.24JAN94             91   24.14
    93    RUNQUALI.SUMARY93      SATLUM93         SATLUM93.25JAN96          89-93   36.34
                                 LUMI93           LUMI93.22OCT95            89-93   36.37
                                 LUMI93FV         LUMI93FV.11FEB94          89-93   36.20
                                 LUMI93RV         LUMI93RV.25FEB94          89-93   36.32
                                 LUMI93_SJAN96    LUMI93_SJAN96.07FEB98     89-93   36.47
    94    RUNQUALI.SUMARY94      STILUM94         STILUM94.03FEB98             91   46.30
    95    RUNQUALI.SUMARY95      STILUM95         STILUM95.03FEB98          89-93   31.72
    96    RUNQUALI.SUMARY96      STILUM96         STILUM96.13SEP96        161-162    9.99
                                 STILUM96_P1      STILUM96_P1.01DEC98         161   10.17
                                 STILUM96_P2      STILUM96_P2.01DEC98     170-172   10.17
                                 STILUM96_Z0      STILUM96_Z0.17JUN97       91-92    0.87
    97    RUNQUALI.SUMARY97      STILUM97_130     STILUM97_130.23OCT97    130-136    6.00
                                 STILUM97_183     STILUM97_183.14DEC98    182-184   54.09
                                 STILUM97_P1      STILUM97_P1.05NOV97     182-184   51.35
                                 STILUM97_Z0      STILUM97_Z0.01AUG97          91    1.59
    98    RUNQUALI.SUMARY98      STILUM98         STILUM98.02MAR99            189  157.65
                                 STILUM98_Z0      STILUM98_Z0.26MAY98          92    1.59
    99    RUNQUALI.SUMARY99      STILUM99         STILUM99.09FEB00        191-202  226.28
    00    RUNQUALI.SUMARY00      STILUM00         STILUM00.28FEB01        200-208  224.23

The `_Z0` sets are the short Z-peak calibration fills taken during LEP2. Within
1996 and 1997 some sets overlap in energy: `STILUM96` covers the same 161 GeV
running as `STILUM96_P1`, and `STILUM97_P1` the same 183 GeV running as
`STILUM97_183`, at slightly lower totals. They are earlier cuts of the same
data, not additional running, and must not be added to it.

Parsing follows PSRUNQ (`skelana.car`): its fixed columns and its file-code
table. A whitespace parse mis-reads the two-letter segment codes, which carry
no separator -- run 51016 segment AA begins `51016AA0`.
