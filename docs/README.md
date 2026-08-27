# Developer notes

Orientation for working on this repository: the build's environment traps,
the architecture that spans several files, and the SKELANA configuration
facts that are expensive to rediscover. User-facing documentation of the
output lives in [`delphi_edm4hep/README.md`](../delphi_edm4hep/README.md);
this file is about *developing* the converter.

## What this is

`delphi_edm4hep` converts DELPHI (LEP) ZEBRA reconstruction output —
shortDST (`.sdst`/`.al`) and fullDST (`.fadana`) — into EDM4hep, plus
`scripts/data-reco/` drivers that reconstruct real DELPHI raw data and feed
the converter.

The converter is a **C++20 wrapper around live Fortran**: PHDST drives the
event loop and SKELANA fills global COMMON blocks, which the writers read.
Almost nothing is re-derived; the output is DELPHI bank values re-expressed
in EDM4hep types. B-tagging is the one deliberate exception, and it is
opt-in — see [SKELANA configuration](#skelana-configuration) below.

## Build and run

```sh
source /cvmfs/delphi.cern.ch/setup.sh                   # needed at BUILD and RUN time
source /cvmfs/sw.hsf.org/key4hep/setup.sh -r 2026-04-08  # order matters: DELPHI first
unset CXXFLAGS CFLAGS LDFLAGS   # MUST precede `cmake -S`; see below
cmake -S delphi_edm4hep -B build && cmake --build build -j
```

One trap, and it costs time if hit: **`unset CXXFLAGS CFLAGS LDFLAGS` is not
optional, and not a `--build`-time fix.** The DELPHI setup exports CERNLIB-era
flags (`-ftemplate-depth-25`, `-ansi`) that CMake bakes into the cache at
**configure** time and that break the podio/C++20 build.

`FindDelphiAL9.cmake` discovers the DELPHI and CERNLIB lib dirs by globbing
the cvmfs release tree and pinning the result, so a cvmfs version bump no
longer breaks configuration. `-DDELPHI_AL9_LIB_DIR=` / `-DCERN_AL9_LIB_DIR=`
still override if needed.

```sh
./build/delphi_sdst_pass  in.sdst  inter.edm4hep.root  [-n N] [--btag off|bank|recalc] [--btag-pv]
./build/delphi_fdst_pass  inter.edm4hep.root[,more...]  in.fadana  final.edm4hep.root  [-n N] [--btag ...]
./build/delphi_bs_fit     final.edm4hep.root  beamspot_by_run.csv    # pure podio, no DELPHI env
./build/delphi_btag_check --source sDST [--primary-vertex-policy keep-delana|replace-with-aabtag] input.edm4hep.root data|NEG_RUN off|bank|recalc [identity-reference.root]
./build/delphi_btag_check --source fDST [--primary-vertex-policy keep-delana|replace-with-aabtag] final.edm4hep.root data|NEG_RUN off|bank|recalc
```

## Tests

`ctest --test-dir build` — CLI-contract checks, pure b-tag domain-boundary
tests, and synthetic ROOT integration cases for clean/dirty `off`, wholly-NaN
and partially-readable historical `bank`, and malformed recalc PV/track
content. `alignment_audit` is a **no-op unless**
`DELPHI_EDM4HEP_SAMPLE=<file.edm4hep.root>` points at a converted file. The
two-prefix real-file integration similarly uses
`DELPHI_EDM4HEP_TWOPASS_SAMPLE`. Single test:
`ctest --test-dir build -R alignment_audit`.

`delphi_edm4hep/tests/align_audit.py` encodes the README's "UserData array X
is index-parallel to collection Y" contracts. Add a row whenever a new
parallel array is emitted.

## Vendored headers, and the reference implementation

`delphi_edm4hep/extern/delphi-analysis/include/` holds the PHDST / SKELANA
COMMON-block wrappers, vendored from `delphi-nanoaod` (there is no longer a
git submodule). Only the headers this converter actually includes are kept,
so **adding a new SKELANA common means vendoring its header too** — that is
how `pscbtg.hpp` arrived with the b-tagging work. See
`extern/delphi-analysis/README.md` for the pinned upstream commit and the
re-copy procedure.

`delphi-nanoaod` itself is still the thing to diff against when SKELANA
behaviour is in question, from a separate checkout:

- `config/delphi-nanoaod.yaml` — the flag and cut values this converter's
  defaults descend from;
- `delphi-analysis/src/skelana_analysis.cpp` — the reference `user00`/`user01`
  sequence;
- `delphi-nanoaod/src/nanoaod_writer.cpp` — note its `Btag_*` field
  *descriptions* are copy-paste-wrong (positive-IP labelled negative); the
  values are filled correctly;
- `delphi-raw-nanoaod/src/raw_nanoaod_writer.cpp` — comments recording
  empirically-verified consequences of individual flags. Useful, but verify
  against the SKELANA source before relying on one: at least one of its
  claims (that `PSBEG` on a non-DST record leaves *stale* state) does not
  match what `skelana.car` actually does.

The DELPHI Fortran sources are readable on lxplus under
`/cvmfs/delphi.cern.ch/releases/almalinux-9-x86_64/latest/dstana/161018/src/car/`
— `skelana.car` and `aabtagxx.car` in particular. They are the authority.

## Architecture

**Two passes, joined by `(run, eventNumber)`.** Pass 1 walks the shortDST and
writes `sDST_*` collections. Pass 2 drives on the fullDST, indexes the pass-1
file by `(run, evt)` in `on_user00`, loads the matching frame *as its own
output frame*, and appends `fDST_*` — so there is no copy-through writer, and
a fullDST event with no pass-1 match is skipped rather than written. PA
indices are **not** stable across DST levels, so pass 2 re-matches tracks by
perigee geometry (`PerigeeMatch`).

**Collection naming** is `<sDST|fDST>_<BANK>_<ReadableName>`, built by
`BankPrefix`. Units are mm / GeV / ns / rad throughout.

**Writers.** Every domain is a `CollectionWriter` subclass with an `emit()`;
the binaries (`bin/delphi_*_pass.cpp`) are an ordered list of writer
constructions per event. **That order is load-bearing** and the binaries
comment why: pass 1 needs Tracking before Vertex/PID/Btag (they consume
`ctx.tracking`); pass 2 needs `MatchProvenanceWriter` first (it fills
`ctx.fdst_pa_to_sdst_*`) and `ShowerHybrid` before `MainHybrid`. Cross-writer
state travels only through the per-event `EventContext`.

**PHDST hooks must live in the binary TU.** `libphdstxx.a` / `libskelanaxx.a`
ship default `user00_`..`user99_` stubs; defining the overrides in the library
would be a double definition. Each binary defines the four `extern "C"` shims
and forwards into `harness::on_userNN`. The DELPHI archives are circular,
hence the `LINK_GROUP:RESCAN` in `FindDelphiAL9.cmake`, and they must come
after the binary's objects.

**Global state.** PHDST/SKELANA are process-global, so `PhdstHarness.cpp`
keeps file-static state, and PHDST reads its input from a `PDLINPUT` text
file the harness writes into the *current working directory* — converter runs
are therefore not safely concurrent in one directory.

**The data fullDST delivers each physical event as ~3 PHDST DST records.**
The harness dedups on `(run, evt)` in `on_user02`; without it every event is
written three times.

**MC vs data divergence is a recurring trap.** Some SKELANA track-impact
commons (`QTRAC(38..40)`, emitted as `sDST_TRAC_d0PV/z0PV/d0BS`) can carry a
`-999` sentinel, which is why `Vertex.cpp` *also* emits the geometric
`sDST_PV_trackD0PV/trackZ0PV/trackImpactFlag`. Those two families use
**opposite sign conventions** (DELPHI vs LCIO) — never mix them. Check for the
sentinel before assuming a common is populated.

The PSCVTX primary slot has its own per-event geometry sentinel. `Vertex.cpp`
falls back to the raw ZEBRA chain (`LQ(LDTOP-1)`) whenever that slot is
unavailable; this is an availability check, not a data-versus-MC classifier.

## SKELANA configuration

`harness::on_user00` does `PHSET("FPE",0)` → `PSINI()` → set `IFL*`. The flag
values descend from `delphi-nanoaod.yaml`. They control which reconstruction
SKELANA *re-runs*, and they change track selection (`LVLOCK`), momenta, and
the primary vertex — not just which commons get filled. Treat any change as a
physics change and validate it against `delphi-nanoaod` output.

### B-tagging

`IFLBTG` / `IFLPVT` are configurable via `--btag` / `--btag-pv` (see
`BtagMode.h` and `delphi_edm4hep/README.md` §2.5). Facts worth not
rediscovering, all from `skelana.car` / `aabtagxx.car`:

- **The `IFLBTG` dispatch is asymmetric.** On a fullDST, `PSHORT` calls
  `PSFBTG` (recalculate) for *any* `IFLBTG > 0`; only on a shortDST does
  `IFLBTG == 1` read the stored bank via `PSHBTG`. Hence the `BTG` (bank)
  vs `AABTAG` (recalculated) naming split.
- **`IFLPVT=1` overwrites `QVTX(6..15,1)`** with AABTAG's vertex, and on the
  beamspot-failure branch (`IERRBS != 0`) writes `-999` over the position,
  destroying the DELANA vertex `PSHVTX` had already filled. This is a
  candidate explanation for the `-999`-on-data behaviour described above;
  legacy sDST files `sDST_EVT_BeamSpotErrorCode` carries `IERRBS`. Current
  output instead records the live value as
  `<source>_BTAGCFG_BeamSpotErrorCode`, which is essential for fDST. Default is
  now `IFLPVT=0`.
- **`KVTX(16,1)` selects the track-selection impact-parameter basis** — PV
  when `<= 0`, beamspot (via `TBDCAE`) otherwise — and `PSFBTG` writes it. So
  `IFLPVT` changes track selection, not only the emitted vertex.
- **AABTAG picks its calibration by the sign of the run number**: `AANAME`
  builds the probability-function filename with `RD` when `nrun > 0` and `MC`
  otherwise. MC with a positive run number silently gets real-data resolution
  functions.
- `PSFBTG` pre-fills the PSCBTG probabilities with `2.0`, so **2.0 means "not
  computed"**, not a probability.

### Known divergences from the reference sequence

These are real behaviour differences from `skelana::Analysis`, not stylistic
ones. They shift track selection, so they deserve their own validation:

- The `PSCUTT` cuts are **not** set, so `IFLCUT=3` uses PSINI's defaults.
  Three differ from the YAML: `TRKLEN(3)` = 0 vs 30 cm (minimum track
  length), `TRKRPH(3)` = 4 vs 10 cm and `TRKZET(3)` = 4 vs 10 cm (maximum
  r-φ and z impact parameters). `TRKLEN` is the consequential one — at 0 it
  keeps short, poorly-measured tracks that land in the impact-parameter
  tails.
- `on_user01` overrides SKELANA's own `USER01` stub with `*need = 1`. That
  stub is where the **record-type gate**, the run-quality selection and the
  bad-1997-HPC skip live, so all three are bypassed. Note `PSBEG` **zeroes**
  every SKELANA common before checking the record type, and only fills for
  `RECTYP == 'DST '` — so a non-DST record leaves the commons wiped, not
  stale, and the writers would run against zeroed input if the `(run, evt)`
  dedup did not usually absorb it.
- `PSBHPC()` is never called even though `IFLBHP=1` (inert for pre-1997
  data).
- `IFLJET` / `IFLENR` are left at PSINI defaults rather than set explicitly.

## `scripts/data-reco/`

Personal integration drivers, not portable tooling — they hardcode absolute
paths (`IMAGE`, `EDMBIN`, EOS/scratch dirs) and run DELANA inside the
`cmssw/el9` singularity image. `run_data_reco.sh` (raw `.sl` → full DST →
optional short DST) feeds `run_data_twopass.sh` (official `.al` for pass 1 +
our full DST for pass 2); `batch_94c.sh` scales that over a worklist from
`build_worklist_94c.sh`. `dump_hadron_tagging.py` operates on
*delphi-nanoaod* output, not on converter output.
