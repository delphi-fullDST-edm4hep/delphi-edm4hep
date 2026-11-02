# delphi-edm4hep

[![CI](https://github.com/delphi-fullDST-edm4hep/delphi-edm4hep/actions/workflows/ci.yml/badge.svg?branch=dev)](https://github.com/delphi-fullDST-edm4hep/delphi-edm4hep/actions/workflows/ci.yml)
[![Nightly key4hep](https://github.com/delphi-fullDST-edm4hep/delphi-edm4hep/actions/workflows/nightly.yml/badge.svg)](https://github.com/delphi-fullDST-edm4hep/delphi-edm4hep/actions/workflows/nightly.yml)
[![docs](https://img.shields.io/badge/docs-API%20%2B%20guides-blue)](https://delphi-fulldst-edm4hep.github.io/delphi-edm4hep/)

The installable software: the dual-pass **DELPHI SDST/FDST → EDM4hep
converter** (`delphi_edm4hep/`) and the data-reconstruction drivers that
feed it (`scripts/data-reco/`).

## Build

```sh
git clone https://github.com/delphi-fullDST-edm4hep/delphi-edm4hep.git
cd delphi-edm4hep

source /cvmfs/delphi.cern.ch/setup.sh                  # DELPHI runtime (pdl2pdl, $DELPHI_DAT)
source /cvmfs/sw.hsf.org/key4hep/setup.sh -r 2026-04-08  # production release
unset CXXFLAGS CFLAGS LDFLAGS                          # MUST precede cmake: the DELPHI env
                                                       # injects -ftemplate-depth-25
cmake -S delphi_edm4hep -B build
cmake --build build -j
```

## Run

```sh
# pass 1: shortDST-level collections
./build/delphi_sdst_pass  simana.sdst  inter.edm4hep.root

# pass 1, resolving a DELPHI dataset nickname instead of a local file
./build/delphi_sdst_pass  -N short94_c2/c1-10  inter.edm4hep.root

# pass 1, from a PDL file (e.g. as produced by `fatfind`)
./build/delphi_sdst_pass  -P dataset.pdl  inter.edm4hep.root

# pass 2: full-DST collections, (run,evt)-matched onto pass 1
./build/delphi_fdst_pass  inter.edm4hep.root  simana.fadana  final.edm4hep.root
```

Source **both** environments at runtime too (DELPHI first, then key4hep —
the converter reads DELPHI files via PHDST). `delphi_edm4hep/README.md`
documents the full collection schema; `scripts/data-reco/README.md`
documents the 94c data drivers and their environment knobs.

## Collection map

**[What is in a converted file](https://delphi-fulldst-edm4hep.github.io/delphi-edm4hep/collection_map.html)**
— every collection, the domain it belongs to, how the collections link to each
other, and where each value came from.

## Dependency versions

This repository does not pin a key4hep stack in its build system. `CMakeLists.txt`
declares the API floors the source needs and reports what the sourced stack
provided; choosing a stack is the caller's job. Configure fails immediately if no
stack is sourced, and the error prints the exact commands to run, including the
current production release.

CI exercises three channels:

| Channel | Stack sourced | When | Role |
| --- | --- | --- | --- |
| `production` | `sw.hsf.org` `-r` the release in `.github/key4hep-production-release` | every PR and push to `dev` | the release the project builds against |
| `stable` | `sw.hsf.org`, no `-r` | every PR and push to `dev` | tracks the current stable release |
| `nightly` | `sw-nightlies.hsf.org` | daily (`nightly.yml`) | early warning that upstream broke the converter |

The stable channel only moves every few months, which is why the nightly channel
exists: it is the one that turns red within a day of an upstream regression.
Nothing is ever pinned to a nightly — cvmfs keeps only about 25 of them.
`bump-check.yml` moves `.github/key4hep-production-release` forward by PR once a
newer stable release has built and tested green. Each CI run's summary records
the concrete release it used.

| Dependency | Requirement | How it is controlled |
| --- | --- | --- |
| key4hep stack | any release meeting the floors below | Sourced by the caller; `$KEY4HEP_STACK` must be set or configure fails (`-DKEY4HEP_STACK_REQUIRED=OFF` to bypass). Printed at configure time. |
| EDM4hep / podio | ≥ 1.0 / ≥ 1.7 | Version floors in `find_package`; the resolved versions are printed at configure time. |
| CMake | ≥ 3.24 | `LINK_GROUP` generator expression. |
| DELPHI Fortran libs + CERNLIB | follows the sourced env (cvmfs `latest`) | `cmake/FindDelphiAL9.cmake` resolves `$DELPHI_LIB` / `$CERN_LIB` to concrete dated cvmfs paths at configure time and prints the pick; override with `-DDELPHI_AL9_LIB_DIR` / `-DCERN_AL9_LIB_DIR`. |
| PHDST/SKELANA wrapper headers | vendored @ `fde7b95f` | In-tree under `delphi_edm4hep/extern/delphi-analysis/` (provenance in its README); override with `-DDELPHI_ANALYSIS_INC`. |

## License

`delphi-edm4hep` is free software, distributed under the GNU General Public
License, version 3 or (at your option) any later version (GPL-3.0-or-later),
inherited from [`delphi/dstana`](https://gitlab.cern.ch/delphi/dstana) and
[`delphi/delphi-nanoaod`](https://gitlab.cern.ch/delphi/delphi-nanoaod). The
PHDST/SKELANA wrapper headers vendored under
`delphi_edm4hep/extern/delphi-analysis/` come from `delphi-nanoaod` and are
distributed under the same licence (provenance in that directory's README).
