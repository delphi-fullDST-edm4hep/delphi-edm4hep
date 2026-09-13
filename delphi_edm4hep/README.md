# delphi_edm4hep

Convert DELPHI ZEBRA reconstruction (shortDST / fullDST) to
[EDM4hep](https://github.com/key4hep/EDM4hep). A per-domain C++ library plus
three command-line tools, organised as a two-pass pipeline.

- **Pass 1** reads a shortDST (`.sdst`) and writes an intermediate EDM4hep
  file with `sDST_*` collections.
- **Pass 2** reads that intermediate file plus the matching fullDST
  (`.fadana`), copies the `sDST_*` collections through, and adds `fDST_*`
  collections (fullDST-only detector detail, plus hybrid collections that
  re-stitch sDST objects onto fullDST tracks).
- A standalone post-processing tool derives a per-run beamspot from the
  reconstructed primary vertices.

Both production passes decode event content without SKELANA and do not link
`libskelanaxx`. A non-installed PSBEG oracle can be built explicitly with
`-DDELPHI_BUILD_SKELANA_REFERENCE=ON` for migration comparisons.

Collection names follow `<source>_<BANK>_<ReadableName>`, where `<source>`
is `sDST` or `fDST`, `<BANK>` is a DELPHI PA-module or historical analysis
mnemonic, and `<ReadableName>` uses DELPHI terminology. Positions are in
**mm**, momenta/energy in **GeV**, times in **ns**, angles in **rad**.

---

## 1. Build and run

### Prerequisites

- A key4hep environment (provides EDM4hep, podio, ROOT):
  ```sh
  source /cvmfs/sw.hsf.org/key4hep/setup.sh -r 2026-04-08
  ```
  `-r 2026-04-08` is the production release, recorded in
  `.github/key4hep-production-release`. Nothing in the build system pins it:
  any stack meeting the version floors (EDM4hep ≥ 1.0, podio ≥ 1.7) works, so
  drop `-r` for the current stable release or source
  `/cvmfs/sw-nightlies.hsf.org/key4hep/setup.sh` for a nightly. Configure
  fails immediately if no stack is sourced at all, and prints the exact
  commands to run (`-DKEY4HEP_STACK_REQUIRED=OFF` to build against an
  EDM4hep/podio provided some other way).
- The DELPHI almalinux-9 Fortran libraries (PHDST / DSTANA and detector
  packages),
  located by `cmake/FindDelphiAL9.cmake`. Source the DELPHI environment
  (`source /cvmfs/delphi.cern.ch/setup.sh`) **before configuring**: the find
  module pins the library directories from `$DELPHI_LIB` / `$CERN_LIB`,
  resolving cvmfs symlinks (`latest`, `pro`) to concrete release paths so a
  later `latest` bump can't silently change or break the build. Without the
  env it falls back to discovery under `-DDELPHI_AL9_ROOT`; override the pins
  directly with `-DDELPHI_AL9_LIB_DIR=...` / `-DCERN_AL9_LIB_DIR=...`.
- The `delphi-analysis` C++ wrapper headers (`phdst/*.hpp`; `skelana/*.hpp`
  is used only by the optional validation-oracle targets)
  are vendored in-tree (`extern/delphi-analysis/`, copied from
  [delphi-nanoaod](https://github.com/DickyChant/delphi-nanoaod) — provenance
  in the README there), so no submodule fetch is needed. Override with
  `-DDELPHI_ANALYSIS_INC=/path/to/delphi-analysis/include` to build against
  an external checkout. (Only the two PHDST-driven passes need these;
  `delphi_bs_fit` does not.)

### Compile

```sh
unset CXXFLAGS CFLAGS LDFLAGS     # see Note — must precede `cmake -S`
cmake -S . -B build
cmake --build build -j
```

> **Note — strip the DELPHI env's compiler flags before *configuring*.** Sourcing
> a DELPHI release setup (`source /cvmfs/delphi.cern.ch/setup.sh`, needed at
> *runtime* for the Fortran libs) exports `CXXFLAGS` / `CFLAGS` carrying
> CERNLIB-era options — notably `-ftemplate-depth-25` and `-ansi` — that break the
> C++20 / podio / nlohmann-json build (`template instantiation depth exceeds
> maximum of 25`). CMake bakes `$CXXFLAGS` into the cache at the **configure**
> step, so they must be stripped there; stripping only at `cmake --build` is too
> late. Either `unset` them as above, or `env -u CXXFLAGS -u CFLAGS -u LDFLAGS
> cmake -S . -B build`.

### Run

```sh
# Pass 1 : shortDST -> intermediate EDM4hep (sDST_* collections)
./build/delphi_sdst_pass  input.sdst  out_sdst.edm4hep.root  [-n MAX_EVENTS] \

# Pass 1, alternative inputs instead of a local file:
./build/delphi_sdst_pass  -N|--nickname short94_c2/c1-10  out_sdst.edm4hep.root  [-n MAX_EVENTS]
./build/delphi_sdst_pass  -P|--pdl dataset.pdl            out_sdst.edm4hep.root  [-n MAX_EVENTS]

# Pass 2 : intermediate + fullDST -> final EDM4hep (sDST_* + fDST_*)
./build/delphi_fdst_pass  out_sdst.edm4hep.root  input.fadana \
                          out_final.edm4hep.root  [-n MAX_EVENTS] \

# Post-processing : per-run beamspot from aggregated primary vertices
./build/delphi_bs_fit     out_final.edm4hep.root  beamspot_by_run.csv

# Prefix-specific all-frame AABTAG validation (production policy shown)
./build/delphi_btag_check --source sDST out_final.edm4hep.root data
./build/delphi_btag_check --source fDST out_final.edm4hep.root data
```

### Native Code4hep source

When this project is added to the Code4hep build through
`CODE4HEP_DELPHI_SOURCE_DIR`, it also builds `DelphiSource` and the
`delphiRun` launcher. The source runs the same converter-owned sDST/fDST
pipelines in memory, publishes each collection as a Stitched event product,
and lets ordinary Code4hep paths consume or write the result:

```python
process.source = cms.Source(
    "DelphiSource",
    input=cms.untracked.string("input.fadana"),
    inputMode=cms.untracked.string("file"),       # file, nickname, or pdl
    conversionPass=cms.untracked.string("sdst"), # sdst or fdst
    intermediateFiles=cms.untracked.vstring(),    # required for fdst
    isRealData=cms.untracked.bool(False),
)
process.output = cms.OutputModule(
    "PodioOutputModule",
    fileName=cms.untracked.string("output.edm4hep.root"),
)
process.end = cms.EndPath(process.output)
```

Run the configuration with `delphiRun config.py`. The dedicated launcher
is required because the non-PIC DELPHI/CERNLIB archives must live in an
executable and export their symbols to the dynamically loaded source plugin.
It links the production archive group, which excludes `libskelanaxx`.

Podio Frame parameters are materialized as reserved framework products while
an event is inside Code4hep, then restored as ordinary Frame parameters by
`PodioOutputModule`. Consequently the DSTQID processing tag, signed DELPHI MC
run number, magnetic field, beamspot, and BTAG configuration retain their
normal `<source>_EVT_*` / `<source>_BTAGCFG_*` names in the output file.

Pass 2 matches each fullDST event to the intermediate frame by
`(runNumber, eventNumber)`, and matches tracks within an event by PA.TRAC
perigee geometry (the PA index is not stable across DST levels).

`-N`/`--nickname` resolves a DELPHI dataset nickname (e.g. `short94_c2` or
`short94_c2/c1-10`) through PHDST's own dataset lookup instead of reading a
local file (writes `FAT = <nickname>` to `PDLINPUT`). `-P`/`--pdl` instead
takes a pre-built PDL file, such as one produced by `fatfind`, and copies it
to `PDLINPUT` verbatim. These two options are mutually exclusive with the
positional `<input.sdst>` argument.

### Tests

```sh
ctest --test-dir build              # everything
ctest --test-dir build -R cli_sdst  # a subset, by name regex
```

Two kinds of test:

- **CLI argument-contract checks** — the passes and checker must reject missing
  or invalid arguments. These need no data files.
- **`tests/align_audit.py`** — audits a converted EDM4hep file for the
  regression class where a UserData array is labelled parallel to the wrong
  collection, or a relation (e.g. RecDqdx → Track) is left unset. It is a
  skipped (exit 77) unless `DELPHI_EDM4HEP_SAMPLE` points at a converted file:
  ```sh
  DELPHI_EDM4HEP_SAMPLE=out_final.edm4hep.root ctest --test-dir build -R alignment_audit
  ```

---

## 2. Output collections
### 2.0 DST flavour and collection naming

A DELPHI DST exists in three flavours, and which PA modules a file carries is
a property of the flavour — not of the year, and not of the reprocessing tag:

| flavour | written by | era |
|---|---|---|
| shortDST | `shortdst.car` | LEP1 |
| longDST | `longdst.car` | LEP1 |
| extended shortDST | `xshortdst.car` | 1996 onward; replaced both |

Each flavour's content is fixed by a production description deck
(`shortdst.des`, `longdst.des`, `DESCRIP`) which marks word by word what is
kept:

| modules | short | long | xshort |
|---|---|---|---|
| `MAIN` `TRAC` `MTPC` `EMNC` `HCNC` `MUID` `ELID` `HAID` `ELTR` `ODHI` `PHOT` `NMUS` `SSTC` `HCRO` | y | y | y |
| `EMCA` `HCAL` `MRIC` `TOF` `TDID` `STIC` `MUFI` `TEID` `TETP` `TEOD` `TEFA` `TEFB` | – | y | y |
| `MU` `EL` `TDHA` `TRAX` `OTRK` `CCAL` | – | y | – |
| `TERF` `TEST` `TEVF` `HCMU` | – | – | y |

`SAT` is kept by the 1993/94 shortDST and the longDST, and disappears once
STIC replaces it.

**The collection name states which flavour supplies it**, so you can tell from
the name alone what an input must be for the data to exist:

    sDST_*    every flavour carries it
    xsDST_*   the extended shortDST and the longDST, but not a plain shortDST
    lDST_*    the longDST only
    fDST_*    written by the fullDST pass

**Companion collections name what they are parallel to.** A `UserData` array
carries no relations and is meaningful only by index, so its name begins with
the collection it is indexed against — `sDST_MAIN_Particles_DetectorMask` is
parallel to `sDST_MAIN_Particles`, `xxsDST_TE_Segments_Length` to
`xxsDST_TE_Segments`.

**Every collection is always emitted**, so the collection set never varies
between samples and code written against one file works on any other. A
collection is simply empty when the input does not carry its module.

**Which modules your file actually had** is recorded in the metadata frame, so
an empty collection never has to be guessed at:

    dst_pa_modules_present        PA modules seen anywhere in the file
    dst_pilot_blocklets_present   PILOT blocklets present

If a module is listed there, an empty collection means that event had no such
activity. If it is not listed, the input never carried it. The census covers
the events actually converted, so a short `-n` run reports only those.

Flavour is not the only reason a collection can be empty. Some content was
dropped by particular productions regardless of flavour:

- `HCNC` is not written from the 1999 processings on; hadron-calorimeter
  clusters come from `HCAL` there.
- The VD dE/dx sub-block of `HAID` is absent from 1999 on, so
  `sDST_HAID_dEdxVD` is empty on those files.
- `MRIC` is absent from the 97E2 processing although its flavour keeps it.

These appear in the census as a missing module, the same as a flavour
difference.

This section is self-contained — it describes the physical content of every
collection and the meaning/units of every field, so the original DELPHI bank
documentation is not needed to use the output.

### 2.0 EDM4hep field conventions used here

How each EDM4hep datatype is populated (units: mm, GeV, ns, rad throughout):

- **Track** — the helix lives in `trackStates`, each a
  `(D0, phi, omega, Z0, tanLambda, time)` tuple with `D0`,`Z0` in mm,
  `omega` (signed curvature) in 1/mm, `phi`/`tanLambda` dimensionless, plus a
  6×6 lower-triangular covariance in those parameters. `chi2`/`ndf` are the
  DELPHI track-fit values. Charge and momentum are **not** on the Track — they
  are on the associated ReconstructedParticle.
- **ReconstructedParticle** — `momentum` (px,py,pz), `energy`, `mass` (GeV);
  `charge` in units of e (0 when the DELPHI charge code is "undefined");
  `tracks` / `clusters` / `particleIDs` relations.
- **Vertex** — `position` (mm); `covMatrix` is the 6-element lower triangle
  `(XX, XY, YY, XZ, YZ, ZZ)` in mm²; `chi2`, `ndf`; `primary` flag;
  `algorithmType`. The meaning of `particles` is collection-specific and is
  documented below: the DELPHI PV chain stores its outgoing-particle
  assignment, while V0 and photon-conversion vertices store fitted daughters.
- **Cluster** — `energy`; `position` (mm) or, for direction-only detectors,
  `iTheta`/`iPhi` (rad); `type` is a sub-detector bit-mask
  (**bit 0 = HPC, bit 1 = EMF, bit 2 = HCAL, bit 3 = STIC, bit 4 = CCA**);
  `subdetectorEnergies` holds the per-layer (HPC) or per-hit (HCAL) energy
  profile; `shapeParameters` holds the parallel layer indices (HCAL) or extra
  scalars.
- **ParticleID** — `algorithmType` identifies the detector/algorithm (values
  given per collection below); `parameters` is an ordered `float` vector whose
  layout is given per collection; `setParticle` links to the
  ReconstructedParticle.
- **TrackerHit3D** — `position` (mm), `cellID`, `type`, `eDep`.
- **CalorimeterHit** — `energy`, `position` (mm), `type`, `cellID`, `time` (ns).
- **MCParticle** — `PDG`, `generatorStatus`, `momentum`/`mass` (GeV),
  `vertex` (production point, mm), `charge`, `parents`/`daughters`.
- **RecoMCParticleLink** — `from` (ReconstructedParticle) → `to` (MCParticle),
  with a `weight`.
- **UserDataCollection&lt;T&gt;** — a flat array stored parallel (index-aligned)
  to the collection named in its description.

### 2.1 Event-level Frame parameters (`sDST_EVT_*`)

Per-event scalars stored as podio Frame parameters:

- Identifiers: `runNumber`, `eventNumber`, `fileSeq`, `date` (yymmdd),
  `time` (hhmmss), `fillNumber` (LEP fill), `experiment`, `dstVersion`.
- Era: `dstProcessingTag` (the DSTQID identifier `YYLN` — two-digit year,
  DELANA processing letter, short/mini DST number, e.g. `94C2`) and
  `pxdstVersion`. Together with `dstVersion` (`ISVER`) these identify which
  DELPHI calibration and algorithm era: the first two characters of the
  processing tag pick the RICH refractive index, and the PXDST version selects
  between algorithm generations.
- Event topology (team-4 reconstruction): `hadronicTagTeam4` (hadronic-Z
  flag), `nChargedTeam4`, `nCharged`, `nNeutral`.
- Energies (GeV): `ECMS` (centre-of-mass), `EChargedTotal`, `ENeutralEM`,
  `ENeutralHad`.
- Beam spot: `BeamSpotX/Y/Z` and `BeamSpotSigmaX/Y/Z` (mm),
  `BeamSpotErrorCode` (0 if the beamspot bank is valid).
- Magnetic field: `BField` (Tesla) and `BFieldGevPerCm` (the
  curvature-to-momentum conversion factor).

**Track selection**

The converter follows the historical `IFLSTR = 11` contract: it flags rejected
tracks rather than removing them, so every reconstructed particle remains in
the output and `sDST_VECP_Particles_SelectionFlag` records the verdict. The cut
table is published per file under the compatibility metadata key
`skelana_IFLCUT`; this is a format name, not a live common-block read.

This converter uses **`IFLCUT = 3`**, the historical default for every year.

| | 1 "old" | 2 "May 98, for 97 data" | 3 "April 99, for 98 data" |
|---|---|---|---|
| min momentum (GeV) | 0.4 | 0.2 | 0.1 |
| max momentum (GeV) | – | 1.5 | 1.5 |
| min track length (cm) | 30 | – | – |
| max \|z\| impact (cm) | 10 | 4 | 4 |
| VD-only, ID+VD without z | kept | rejected | rejected |
| HCAL noise, STIC off-momentum | off | off | on |
| neutral HPC/FEMC/HCAL/STIC (GeV) | ~0 | .5/.4/.9/.3 | .3/.4/0/.3 |

Table 3 is not uniformly the loosest — looser on momentum and length, stricter
on seven other cuts, notably the neutral calorimeter thresholds.

**Two selections coexist in one file and must not be mixed.** The event
counters `nChargedTeam4` and `hadronicTagTeam4` come from `PSHEVT`, which is
hardwired to table 1 and ignores `IFLCUT` — so a file reports a table-1 track
count beside a table-3 per-track flag.

To apply a different table yourself, the inputs are all in the output:
momentum and the impact parameters, plus `sDST_MAIN_Particles_TrackLength`
(the only cut quantity with no other route) and
`sDST_MAIN_Particles_ReconstructionCode` (the word SKELANA reads to reject
VD-only and ID+VD-without-z tracks).

### 2.2 Pass-1 collections (`sDST_*`, `xsDST_*`, `lDST_*`)

**Truth**

- `sDST_LUJ_GenParticles` (MCParticle) — the generator (LUND/JETSET) event
  record: `PDG`, `generatorStatus`, momentum/mass (GeV), production vertex
  (mm), charge, and parent links.
- `sDST_TBL_RecoToGen` (RecoMCParticleLink) — exact reconstructed→generated
  correspondence from the DELPHI association tables (not a geometric match);
  `from` = `sDST_MAIN_Particles`, `to` = `sDST_LUJ_GenParticles`.

**Tracks & particles**

- `sDST_TRAC_Tracks` (Track) — one charged track per reconstructed charged
  particle. An `AtIP` TrackState carries the perigee helix
  (`D0` = −ε with DELPHI→EDM4hep sign flip, `Z0`, `phi`, `omega`, `tanLambda`)
  with the 5×5 covariance obtained by inverting the DELPHI weight matrix and
  rotating into helix parameters. `chi2`/`ndf` are the with-VD fit when
  available, else the without-VD fit. `tracks` and `trackerHits` link to the
  track elements from the same PA; further TrackStates carry the
  extrapolations below.

  > **Extrapolation states.** `PA.TRAX` holds the track extrapolated onto its
  > own first measured point and a set of named detector surfaces, ordered by
  > increasing R in the barrel and |Z| in the endcap. Each becomes a TrackState
  > whose `referencePoint` is the extrapolated point and `phi`/`tanLambda` the
  > direction there; `D0` and `Z0` are zero at that point by construction.
  >
  > `location` distinguishes them: `AtFirstHit` is the track's first measured
  > point, `AtCalorimeter` the HPC, HAB, HAF and EMF crossings, and `AtOther`
  > the TOF and muon-chamber surfaces. EDM4hep has no field for the detector
  > itself, so an individual surface is identified by radius — they are well
  > separated, and the ordering above is the physical crossing order.
  >
  > Covariance is present only where DELPHI stored one: on the first measured
  > point for essentially every track, and on the muon surfaces when the track
  > has an associated MU signal. Elsewhere it is zero.
- `sDST_QTRAC_Tracks_d0PV`, `sDST_QTRAC_Tracks_z0PV`, `sDST_QTRAC_Tracks_d0BS` (UserData&lt;float&gt;) —
  impact parameters of each track w.r.t. the primary vertex (`d0PV`, `z0PV`)
  and the beam spot (`d0BS`), mm; parallel to `sDST_TRAC_Tracks` (charged
  only — neutrals have no entry), NaN when no PV/BS-corrected value is
  available for that track.
- `sDST_VECP_Particles_SelectionFlag` (UserData&lt;int32&gt;) — per-particle
  lock/status mask reconstructed by the converter; bit 1 marks selection
  failure and bit 32 multi-vertex/REMCLU locking. −1 marks a particle for which
  no direct view could be built. Parallel to `sDST_MAIN_Particles`.
- `sDST_MAIN_Particles_ReconstructionCode` (UserData&lt;int32&gt;) — raw PXPHOT code,
  parallel to `sDST_MAIN_Particles`.

  > **A code describing how the track was reconstructed** — e.g. 75 = VD-only
  > with Z, 72 = ID+VD-only without Z, 120 = incoming track to a hadronic
  > interaction. Stored raw, as DELPHI wrote it.
  >
  > **Its meaning is not portable between processings.** Which codes appear at
  > all was a production choice: 76 occurs only in 1999–2000, and 77 is absent
  > from 97E2 and 98C2 but common either side. DELPHI changed convention part
  > way through, keyed on `sDST_EVT_dstProcessingTag` (new from `96`) — use
  > that, not `sDST_EVT_dstVersion`, to decide how to read the code.
- `sDST_MAIN_Particles_TrackLength` (UserData&lt;float&gt;) — track length in cm, parallel
  to `sDST_MAIN_Particles`.
- `sDST_MAIN_Particles_DetectorMask` (UserData&lt;int32&gt;) — which subdetectors were used
  in this particle's reconstruction, a raw bit mask parallel to
  `sDST_MAIN_Particles`. Bits: 2 primary vertex in fit, 4 VD, 5 ID, 6 TPC,
  7 RIB, 9 OD, 10 HPC, 14 HAB, 15 MUB, 19 STIC, 21 FCA, 22 RIF, 23 HAF,
  26 FCB, 27 EMF, 31 MUF.

  > **Bit 4 is "VD or VFT", not VD alone** — DELPHI's own code says so
  > (`mammoth.car:13556`), though the content note predates the VFT and still
  > calls it VD. At LEP2 it is set on every VFT-reconstructed track, so a
  > "has VD hits" cut written against it silently widens.
- `sDST_MAIN_Particles` (ReconstructedParticle) — charged and neutral
  particles. The converter reconstructs the historical combined momentum from
  PA `MAIN`/`TRAC`, including the charged-track refit and recovery paths and
  their mass hypothesis. `charge` = +1/−1 from the DELPHI charge code; the
  "undefined" code maps to 0. Charged particles link to their
  `sDST_TRAC_Tracks` entry.

**Vertices**

- `sDST_PV_PrimaryVertex` (Vertex) — the event primary vertex (position + 6
  covariance terms in mm², `chi2`, `ndf`, `primary=true`). The collection is
  empty when DELPHI's first vertex slot has the dummy or secondary status bit;
  a dummy beam-spot bucket is never labelled as a fitted primary vertex.
- `sDST_PV_Vertices` (Vertex) + `sDST_PV_Vertices_StatusBits`
  (UserData&lt;int32&gt;) — the full vertex chain (primary + secondary +
  simulation vertices); the parallel array carries the raw DELPHI per-vertex
  status word (dummy / secondary / hadronic-secondary / simulation /
  flavour-tag bits). For reconstructed-chain entries, `algorithmType = 0`
  means the usable event primary and `algorithmType = 1` means non-primary
  reconstructed content (including dummy beamspot buckets); the raw status
  word carries the finer classification. For reconstructed PV-chain entries, `particles` is the
  raw DELPHI **outgoing-PA assignment**, which can partition the event; it is
  not the set of tracks used by the vertex fit. In particular, do not use this
  relation for b-tag fit membership. AABTAG publishes its own vertex, and its
  `sDST_AABTAG_TrackTag` rows carry an attached-to-PV flag.
- `sDST_BSP_BeamSpot` (Vertex, 1 entry) — the official beamspot: position with
  a diagonal covariance built from the beam widths; `algorithmType = 2` marks
  "beamspot bank, not a fit". (See also `delphi_bs_fit` in §3.)
- `sDST_V0_V0Candidates` (Vertex) — the official DELPHI V0 vertices (K⁰s / Λ /
  γ-conversion candidates); `position` from the V0 fit, with the two daughter
  particles in the `particles` relation. (Covariance is left zero — the bank's
  weight matrix is in a non-standard basis.)
- `sDST_PHC_PhotonConversions` (Vertex) — photon-conversion (γ→e⁺e⁻) vertices,
  e⁺/e⁻ in the `particles` relation.

**Calorimeter showers**

- `sDST_EMNC_Showers` (Cluster) — electromagnetic showers (HPC barrel and FEMC
  endcap, distinguished by `type` bits 0/1); `energy`, `position`, and the
  per-layer energy profile in `subdetectorEnergies`.
  `hits` links the HPC pad hits of the same shower (`xsDST_EMCA_HPCClusters`).
  Only showers the EMCA module records as HPC carry them.
- `sDST_HCNC_Showers` / `xsDST_HCAL_Showers` (Cluster) — hadron-calorimeter
  showers (`type` bit 2); per-hit energies in `subdetectorEnergies`, the
  parallel layer indices in `shapeParameters`. **Both are always emitted and
  either may be empty — see the note below before using them.**

> **Hadron calorimetry — which collection to read**
>
> DELPHI wrote the hadron calorimetry in two PA modules, and *which ones are on
> the file is a per-processing choice*, not an era or format property. There is
> no version word that predicts it: 95D1 and 96F1 share both `ISVER` (105) and
> PXDST version (336) yet differ. Both collections are therefore always
> emitted, and either may be empty.
>
> | processing | `HCNC` | `HCAL` |
> |---|---|---|
> | 94B3 | filled | filled |
> | 92E2, 94C2, 95C2, 95D1 | filled | empty |
> | 96F1, 97E2, 98C2, 98D1 | filled | filled |
> | 99C1, 99D1, 99E1, A0C1, A0E1 | empty | filled |
>
> `HCNC(23)` is written during shortDST production: `HCAL(3)`'s hits after
> HACFIX, re-associated between charged tracks and neutrals by HACCOR, with
> recovered neutral showers added. `HCAL(3)` is the uncorrected parent copied
> down from DELANA. **Where both are filled they are not duplicates, and
> `HCNC` is the one to use** — that is the precedence DELPHI's own code
> applies (`PSHHAC`; `ECORR`, *"HCNC module overwrites module 3 info"*).
>
> The difference is not small: in 94B3 the leading-shower energy agrees on only
> 83 of 1912 overlapping tracks, mean |ΔE| 0.6 GeV and up to 65 GeV; in 96F1 on
> 580 of 618, mean |ΔE| 0.18 GeV.
>
> **From the 1999 processings onward only `HCAL` is written.** What that means
> for the HACCOR correction on those files is not established here: `HCNC` is
> the module HACCOR produces and it is absent, but whether the correction is
> reflected elsewhere on the file has not been determined. An analysis spanning
> 1998 and 1999 should not assume the two years carry the same quantity. The
> table lists every processing measured; for one not listed, check which
> collections are populated rather than inferring from the year.

**Particle identification** (all ParticleID; `setParticle` → `sDST_MAIN_Particles`)

- `sDST_GETDEDX_Dedx` / `sDST_BBDXGET_Dedx` (algType 1) — TPC truncated-mean
  dE/dx, named for the routine that produced it. BBDXGET replaced GETDEDX at
  PXDST version 333; the version word of the event selects one, and only that
  one is emitted. `params`: `[0]` dE/dx (MIP-normalised), `[1]` σ, `[2]` quality
  flag (−1 no information, 0 poor … 4 perfect; requiring > 1 is the usual cut).
  Also emitted in EDM4hep's typed `RecDqdx` form as `..._DedxRecDqdx` (type 1).

  On the BBDXGET path the wire count reported through `sDST_HAID_HadronID` and
  the dE/dx tag collections is the *untruncated* count scaled by 0.8 — an
  estimate of the wires entering the truncated mean, not a count of them. At
  LEP1 (GETDEDX) it is a true count. Take care when comparing a wire-count cut
  across eras.

  The quality flag is recovered directly by this converter; it was computed
  but discarded by the historical analysis path.
- `sDST_HAID_HadronID` (algType 4) — combined hadron ID, 18 params:
  `[0]` kaon-RICH tag, `[1]` proton-RICH, `[2]` pion-RICH, `[3]` kaon-dE/dx,
  `[4]` proton-dE/dx, `[5]` combined kaon likelihood, `[6]` combined proton
  likelihood, `[7]` RICH quality, `[8]` θ_C gas (rad), `[9]` σθ gas,
  `[10]` N photo-electrons gas, `[11]` N expected gas, `[12]` gas flag
  (`FLAGG` = `KGRIC(5)`, RING/VETO quality word carried as a float; read
  via `int(round(v))`), `[13..16]` the same four quantities for the liquid
  radiator, `[17]` liquid flag (`FLAGL` = `KLRIC(5)`, same convention).
- Recomputed hadron-ID tag tables, one collection per DELPHI routine. Each
  tag: −1 = no info, 0 = not this species, 1/2/3 = loose/standard/tight. A
  row is emitted for a track when at least one of its tags is not −1.
  - `sDST_XNEWTAG_RichTags` (algType 41) — RICH ring/veto tags. `params`:
    π/K/p/heavy `[0..3]`, matching track-quality acceptance `[4..7]`.
  - `sDST_XNEWPRO_RichTags` (algType 42) — RICH RIBMEAN probability tags.
    `params`: π/K/p/heavy/electron `[0..4]`, selection flag `[5]`
    (bit 1 liquid OK, bit 2 gas OK).
  - `sDST_RPRODO_DedxTags` (algType 43) / `sDST_RPRODE_DedxTags` (algType 44)
    — TPC dE/dx probability tags. RPRODE superseded RPRODO at PXDST version
    333; the version word of the event selects one, and only that one is
    emitted, so the collection name states which ran. `params`:
    π/K/p/heavy/electron `[0..4]`, quality flag `[5]` (1 = more than 30 TPC
    wires and within 2.5 s.d. of a hypothesis).
  - `sDST_RPROCO_CombinedTags` (algType 45) — combined RICH and dE/dx tags.
    `params`: π/K/p/heavy/electron `[0..4]`, selection flag `[5]`
    (bit 1 liquid OK, bit 2 gas OK, bit 3 TPC OK).
- `sDST_HAID_dEdxVD` (algType 7) — VD-only dE/dx. `params`: `[0]` VD dE/dx,
  `[1]` number of VD hits used.

  > **Empty from the 1999 processings** — the VD dE/dx sub-block of `HAID(26)` is
  > not written there (`IDATV = 0` in the packed size word `Q(LHAID+2)`), and it is
  > not recoverable: the routine that computes it needs VD pulse heights, which the
  > shortDST does not carry. From 1996 the per-hit VD signal-to-noise ratio is
  > available instead, in `eDep` on `sDST_TDVD_VDHits` / `_VDPoints`.
- `sDST_MUID_MuonID` (algType 2) — `[0]` muon tag (MUCAL2: 1 very-loose …
  4 tight, 5 HCAL), `[1]` global χ² of the very-loose refit, `[2]` hit pattern.
- `sDST_ELID_ElectronID` (algType 3) — `[0]` electron tag (0 not run, 1 not-e,
  2 very-loose, 3 loose, 4 standard, 5 tight), `[1]` γ-conversion tag.
- `sDST_PHOT_PhotonID` (algType 30) — HPC photon-ID scores: energy-weighted
  shower depth, n clusters, first layer, n layers, max consecutive layers,
  transverse fluctuation, longitudinal-fit value (up to 7 params).
- `sDST_PHOT_Pi0ID` (algType 111) — π⁰→γγ HPCANA fit, 26 params (tensor-fit
  mass / rotation / eigenvalues, connected & expected maxima, two-Gaussian fit
  parameters, shower θ/φ, OD-link & stray-shower counts, fit χ², σφ/σθ).
- `sDST_ODHI_OuterDetector` (algType 29) — Outer-Detector per-track hit
  summary (up to 7 raw bank words).
- `xsDST_MUFI_RefitMuon` (algType 27) — refitted-muon fit summary: detector
  (14=MUB/17=MUS/30=MUF), n layers, ndof, global χ², first-layer x/y,
  expected-missing-layers, chambers-alone χ², extrapolated x/y/θ/φ and their
  errors.
- `sDST_HCRO_HitPattern` (algType 34) — hadron-calorimeter hit pattern for the
  track: `[0]` tube hits, `[1]` mean distance from the track extrapolation to
  the fired tubes (mm), `[2]` its rms, `[3..22]` hits in planes 1–20. The
  spread across planes separates penetrating tracks from showers.
- `sDST_HCRO_MuonTag` (algType 34) — muon block carried by some HCRO modules:
  `[0]` identification flag, `[1]` azimuth (deg), `[2]` χ², `[3]` rms of the
  hit-to-track distance, `[4]` packed status word.
- `xsDST_MRIC_RichExtrapolation` (algType 6) — the RICH measurement PXDST
  records for the track, independent of the Cherenkov angles in
  `sDST_HAID_HadronID`: `[0]`/`[1]` liquid and gas radiator refractive index
  (NaN when that radiator did not fire), `[2]`/`[3]` extrapolation coordinates
  at the RICH entry (mm), `[4]`/`[5]` polar and azimuthal angle (rad),
  `[6]` q/p (1/GeV), `[7..11]` variances of `[2..6]`, `[12]` ionization hits.
  `[2]`/`[3]` are R·φ and z for a barrel track and x and y for a forward one;
  the module carries no flag saying which, so use the polar angle. Some stored
  words are omitted: their meaning is not established for this PXDST version.
- `xsDST_HCMU_MuonID` (algType 35) — muon tag from the HCRO pattern, comparing
  hits, penetration, timing and energy against calibrated expectations per
  polar angle: `[0]` level (1 or 3; 2 does not occur), `[1]` mean distance
  (mm), `[2]` its rms.

**Other detectors**

- `sDST_ELTR_RefitTracks` (Track) + `sDST_ELTR_RefitTracks_ParticleIndex`
  (UserData&lt;int32&gt;) — the electron-hypothesis refitted track (same perigee
  + covariance treatment as `sDST_TRAC_Tracks`); the parallel index gives the
  `sDST_MAIN_Particles` entry it belongs to.
- `sDST_SSTC_Showers` (Cluster) — STIC (small-angle) calorimeter showers:
  `energy`, direction `iTheta`/`iPhi`; `type` bit 3.

  > **Two modules, one detector.** `STIC(19)` is the fullDST module, `SSTC(33)` a
  > condensation made at shortDST production; the converter takes `STIC` when
  > the file has it and `SSTC` otherwise (the historical `PSHSTC` rule, with no
  > version gate), and the collection is
  > named `SSTC` either way. On the `SSTC` path — 94C2, 95C2, 95D1 only —
  > `energy`/`iTheta`/`iPhi` are the track's MAIN kinematics rather than STIC
  > measurements, `shapeParameters[0]` is scaled 1/10 not 1/1000, `[1]` is a
  > photon/electron code not a veto tag, and `[3]`/`[4]` are absent.
**Track elements**

- `xsDST_TE_Segments` (Track) — one per track element, each carrying a single
  `TrackState` at `AtOther`: `referencePoint` is the measured point, `phi` the
  track direction there, and `chi2`/`ndf` the element's own fit quality.
- `xxsDST_TE_Segments_Length` (UserData&lt;float&gt;) — element length in mm, parallel
  to `xsDST_TE_Segments`.
- `xsDST_TEVF_TrackElementPlane` (TrackerHitPlane) + `xxsDST_TEVF_TrackElementPlane_Length`
  — the very forward tracker, which measures two coordinates and no direction.

  Reach them from `sDST_TRAC_Tracks.tracks` and `sDST_TRAC_Tracks.trackerHits`.

  > **`type` names the source module**, as `label*10 + stage` — the bank
  > mnemonic is not in the collection name, so this is what identifies it:
  >
  > | `type` | bank | detector | stage digit |
  > |---|---|---|---|
  > | 12 | `TEID` | inner detector | 1 jet chamber, 2 trigger layer |
  > | 13 | `TETP` | TPC | |
  > | 14 | `TEOD` | outer detector | |
  > | 15 | `TEFA` | forward chamber A | |
  > | 16 | `TEFB` | forward chamber B | |
  > | 21 | `TERF` | forward RICH | |
  > | 41 | `TEST` | straw tubes | |
  > | 42 | `TEVF` | very forward tracker | |
  >
  > So 121 is an inner-detector element from the jet chamber and 131 a TPC one.
  > The stage digit is 0 on files written before PXDST 2.87.
  >
  > **Detectors measure different quantities, and what a module did not measure
  > is `NaN` — never 0, which is a legal measured value.** `D0` and `Z0` are
  > always `NaN`: a track element measures a point, not an impact parameter.
  > Typically the TPC gives direction and curvature, the inner detector
  > curvature but not the dip angle, and the outer detector, forward RICH and
  > straw tubes direction only. It varies element by element, so test for `NaN`
  > rather than inferring from `type`.

- `sDST_TDVD_VDPoints` (TrackerHit3D) — unassociated Vertex-Detector hits.
- `sDST_TDVD_VDHits` (TrackerHit3D) — Vertex-Detector hits associated to a
  track, reachable through `Track.trackerHits`. For both collections
  `position` is cylindrical-mixed `(R, slot2, slot4)` in mm, not Cartesian:
  the module→φ table for global (x,y) is not on the DST. `cellID` is the
  signed module number (sign = Z side), `type` bit 0 marks an R-Z measurement,
  `eDep` the signal-to-noise ratio.
- `xsDST_PXTD_PixelHits` (TrackerHitPlane) — Very Forward Tracker pixel
  clusters: `position` in the DELPHI frame (mm), `du`/`dv` the measurement
  errors along the module axes (mm), `cellID` the module number. `u`/`v` are
  NaN, since the module orientation is not on the DST. Companions
  `_ClusterSize` and `_TanagraId` give the pixels in the cluster and the track
  element it was assigned to. Empty on LEP1, where the VFT did not exist. The
  per-pixel addresses and the ministrip bank STTD are not converted.

### 2.3 Pass-2 pure-fullDST collections (`fDST_*`)

- `fDST_MAIN_Particles_MatchProvenance` (UserData&lt;int32&gt;, parallel to
  `fDST_MAIN_Particles` — one entry per reconstructed particle, charged and
  neutral, NOT parallel to `fDST_TRAC_Tracks` which is charged-only) —
  per-particle provenance from the pass-2 perigee match: `+1` charged
  particle matched to a fullDST PA, `0` charged particle with no fullDST
  counterpart (post-DST V0 daughter, photon-conversion daughter, or hadronic
  secondary that exists only in the shortDST), `−1` neutral particle (no
  perigee match applies).
- `fDST_TRAC_Tracks` (Track) — the shortDST track cloned with its `AtIP` state,
  plus the extrapolation states the fullDST carries: one `AtFirstHit` at the
  first measured point, `AtCalorimeter` at a calorimeter face, and `AtOther`
  elsewhere along the track (mm). Track elements are linked rather than merged
  in — see `fDST_TE_Segments` and `fDST_TEVF_TrackElementPlane`.
- `fDST_TOF_TimeOfFlight` (ParticleID, algType 5) — `[0]` time of flight (ns),
  `[1]` σ_t (ns).
- `fDST_MTPC_dEdxExtended` (ParticleID, algType 6) + `fDST_MTPC_dEdx_RecDqdx` —
  extended TPC dE/dx, 10 params: `[0]` 80%-truncated mean, `[1]` σ80,
  `[2]` 65%-truncated mean, `[3]` σ65, `[4]` integrated 80% dE/dx, `[5]` n pads,
  `[6]` n wires, `[7]` n saturated, `[8]` n empty, `[9]` pad-row pattern.
- `fDST_MU_MuonChambers` (ParticleID, algType 4) — raw muon-chamber refit
  summary, 12 params: detector, n layers, ndof, global χ², first-layer x/y,
  expected hit pattern, chambers-alone χ², extrapolated x/y/θ/φ.
- `fDST_EL_ElectronExtra` (ParticleID, algType 5) — `[0]` detector id
  (9 HPC / 26 EMF), `[1]` number of showers.
- `fDST_TDID_DriftCalib` (ParticleID, algType 17) — `[0]` signed jet sector,
  `[1]` number of valid drift wires, `[2]` sum of drift times.
- `fDST_EMCA_HPCClusters` (CalorimeterHit) — per-pad HPC: `energy` =
  photo-electrons, `energyError` = σ_z, `type` = layer (1..10), `position` mm.
- `fDST_EMCA_FEMCLayers` (CalorimeterHit) — per-layer FEMC: `energy` = layer
  energy, `type` = layer, `cellID` = n hits, `position` = shower centroid.
- `fDST_HCAL_Towers` (CalorimeterHit) — per-tower HCAL: `energy` = tower
  amplitude, `type` = layer, `cellID` = packed `LAY·10000 + JU·100 + JV`.
- `fDST_TDHA_HcalTimeHits` (CalorimeterHit) — per-layer HCAL time-digitisation
  hits: `energy` = TD energy, `type` = HCAL layer, `cellID` = 0 barrel/1
  endcap, `position` mm (barrel R/Rφ converted to x,y); no per-hit time at this
  DST level.
- `fDST_TEAD_TOFHits` (CalorimeterHit) — unassociated barrel-TOF hits
  (neutral candidates): `position` mm (R/Rφ→cartesian), `time` (ns).
- `fDST_TEAD_HOFHits` (CalorimeterHit) — unassociated forward-HOF hits:
  `position` mm; time not decoded at this DST level.
- `fDST_STIC_Showers` (Cluster) — STIC showers from the fullDST (same content
  as `sDST_SSTC_Showers`).

### 2.4 Pass-2 hybrid collections (`fDST_*`)

These are element-by-element copies of the corresponding `sDST_*` collection
with their cross-collection relations re-pointed onto the fullDST objects, so
the file holds two internally-consistent views (a pure-shortDST view and a
shortDST+fullDST view). Field contents are otherwise identical to the `sDST_*`
originals described in §2.2.

- `fDST_MAIN_Particles` — `tracks` now reference `fDST_TRAC_Tracks`.
- `fDST_PV_PrimaryVertex`, `fDST_PV_Vertices`, `fDST_V0_V0Candidates`,
  `fDST_PHC_PhotonConversions` — `particles` re-pointed to `fDST_MAIN_Particles`.
- `fDST_HAID_HadronID`, `fDST_MUID_MuonID`, `fDST_ELID_ElectronID`,
  `fDST_<algo>_Dedx` (+ `_DedxRecDqdx`) — `setParticle` re-pointed to
  `fDST_MAIN_Particles`. `<algo>` mirrors pass 1 (BBDXGET or GETDEDX).
- `fDST_EMNC_Showers`, `fDST_HCNC_Showers`, `fDST_HCAL_Showers` — shower clones.
- `fDST_TE_Segments`, `fDST_TEVF_TrackElementPlane` — track elements decoded
  from the fullDST PA chain, linked from `fDST_TRAC_Tracks`. `ndf` and `chi2`
  are native on the segments, the TE descriptor is in `quality`, and the
  element length is the parallel `fDST_TE_Segments_Length`.
- `fDST_TBL_RecoToGen` — `from` re-pointed to `fDST_MAIN_Particles`.

### 2.5 B-tagging

DELPHI's b-tag comes in two forms, both emitted on every file:

- `<prefix>_BTG_*` — the tag stored on the DST (`Transcribed`). NaN where the
  BTAG bank was not written, which includes many shortDSTs.
- `<prefix>_AABTAG_*` — the tag recalculated at conversion time (`Derived`).

Prefer `AABTAG_*` for analysis: it describes data and simulation more
consistently. `BTG_*` is what DELPHI recorded, for reproducing published work.

Both carry `ProbNegIP`, `ProbPosIP`, `ProbAllIP` (triplets: hemisphere 1,
hemisphere 2, whole event), `ThrustAxis` and `ThrustValue`. The per-track
quantities and AABTAG's primary vertex exist only for `AABTAG_*`; the stored
bank has no per-track content. AABTAG's vertex is a separate collection and
does not replace `sDST_PV_PrimaryVertex`.

> On simulation, AABTAG draws from the same random sequence as detector
> smearing, so per-event values differ from a conversion made without it.
> Distributions are unchanged; real data is unaffected.

Every frame carries source-local provenance:

- `<prefix>_BTAGCFG_SourcePrefix`, which must equal the collection prefix;
- `<prefix>_BTAGCFG_BeamSpotErrorCode`, the live `IERRBS` for that pass (do not
  substitute the copied `sDST_EVT_*` value when validating fDST content).

> **1996 data carries no b-tagging, in either form.** The stored `BTG` bank was
> not written by that production, and the beamspot database has no entry for
> those runs, so `IERRBS != 0` and the converter skips AABTAG recalculation.
> Both collections are present and empty. This follows the run, not the DST
> flavour: short and long 1996 samples behave alike.

`delphi_btag_check` validates the payload: both tags present with their
probabilities and thrust in range, one `AABTAG_TrackTag` row per track AABTAG
reports with every value in its allowed domain and a resolvable particle, an
`AtVertex` state per track, and a vertex whose attached-particle count matches
`NTracksAttached`. A vertex must be primary, carry `algorithmType = 3`, and
have a finite position; its chi2 is checked only where `ndf > 0`, since AABTAG
reports beamspot-only vertices with no fit.

This makes the two pass-2 b-tag payloads independently auditable even though
event identity remains in `sDST_EVT_runNumber/eventNumber/fileSeq`; pass 2 does
not emit an `fDST_EVT` namespace.

**Event-level** (frame parameters, both modes). Each probability triplet is
ordered *(hemisphere 1, hemisphere 2, whole event)*:
`ProbNegIP`, `ProbPosIP`, `ProbAllIP`, `ThrustAxis` (3 components),
`ThrustValue`. `PSFBTG` pre-fills these with `2.0` and only overwrites them
when the beamspot is usable, so **2.0 is a "not computed" marker**. LUTHRU also
uses thrust values `-1` and `-2` for failure. These sentinels are mapped to NaN
on output.

**Per-track and vertex** (`recalc`, or `bank` on the fullDST). From AABTAG's
`AAMAIN` / `AAMNVX` commons:

- `<source>_AABTAG_PrimaryVertex` (Vertex, 1 entry, `algorithmType = 3`) —
  AAMNVX's vertex output, emitted *alongside* `sDST_PV_PrimaryVertex` rather
  than replacing it. `particles` links the tracks AABTAG attached to the fit. Treat it as a valid fit only when its coordinates and
  covariance are finite, `<source>_AABTAG_Valid == 1`,
  `NTracksAttached > 0`, and `ndf > 0`.
  A status-zero entry with no attached tracks/ndf is a beamspot-only result,
  not a track-fitted PV. The collection is empty when `Valid != 1`.
- `<source>_AABTAG_TrackTag` (ParticleID, `algorithmType = 4`) — one row per
  track AABTAG used, linked to its particle. `params`: `[0]`/`[1]` track
  probabilities Rφ and z, `[2]` χ² to the VD, `[3]` χ² to the primary vertex
  (NaN unless `[10]` is set; may be slightly negative), `[4]` momentum, `[5]`/`[6]`
  VD hits Rφ and z, `[7]`/`[8]` VD layers Rφ and z, `[9]` used for the tag,
  `[10]` attached to the vertex. The hit and layer counts are signed: a
  negative value marks a rejected track, `abs()` is the count.

  Impact parameters are on the track: each track AABTAG used carries a
  `TrackState` at `AtVertex`, with `referencePoint` at AABTAG's vertex, `D0`
  and `Z0` in mm and their variances in the covariance. Unmeasured components
  are NaN. Reach it with `getParticle()` → `getTracks()`.

  > `D0` uses the EDM4hep sign convention, opposite to AABTAG's. `Z0` is
  > unchanged.
- Frame parameters `BadEventCode`, `AlgorithmInvoked`, `Valid`, `NTracksRaw`,
  `NTracks`, `NTracksAttached`, `Truncated`. `BadEventCode` preserves AABTAG's
  raw `IBAD` snapshot (0 success, 1 processing failure, 2 vertex-fit failure),
  without inventing a converter-specific value. It describes the current event
  only when `AlgorithmInvoked == 1`. PSFBTG skips AABTGS when the same-source
  `<source>_BTAGCFG_BeamSpotErrorCode != 0` and leaves `IBAD` stale, so `Valid` is the
  authoritative combined gate:
  `Valid = AlgorithmInvoked && BadEventCode == 0`. When `Valid == 0`, the event
  probabilities/thrust are NaN, the PV and per-track collections are empty,
  and `NTracks = NTracksAttached = Truncated = 0`. `NTracksRaw` preserves the
  saturated current-event count when the algorithm ran and is zero when it was
  skipped. This prevents stale COMMON values from a preceding event being
  published as valid while retaining the raw legacy diagnostic. AABTAG's
  arrays are dimensioned 100 tracks and its counter saturates there;
  `Truncated = 1`
  therefore means the capacity was reached and additional eligible tracks may
  have been clipped, not that clipping can be proved from the common alone.

`ImpactParRPhi` uses AABTAG's **own sign convention**, not the LCIO `D0` sign
of the Track collections — the sign is the physics (the negative-IP side is
the mistag control sample). Do not mix it with `sDST_QTRAC_Tracks_d0PV` or
`sDST_PV_Tracks_d0PV`; see §2.2.

---

## 3. Code structure

The translation logic is a library (`libdelphi_edm4hep`) of per-domain
*writer* classes over shared infrastructure; the binaries are thin harnesses.

```
delphi_edm4hep/
├── include/delphi_edm4hep/
│   │   Helix.h  CollectionWriter.h  PhdstHarness.h  BankPrefix.h
│   │   PerigeeMatch.h  TeBank.h  internal/{PaWalk,HpcPadDecoder}.h
│   ├── Event/  Truth/  Tracking/  Vertex/  Calorimeter/  Pid/   (writer headers)
├── src/   (mirrors include/; shared .cpp at top level, writers in src/<Domain>/)
└── bin/   delphi_sdst_pass.cpp  delphi_fdst_pass.cpp  delphi_bs_fit.cpp
```

Each writer subclasses `CollectionWriter`, reads one DELPHI domain, and emits
its collections; the binaries simply run the writer chain for their pass.
Cross-writer state (truth links, the tracking output, the PA→particle maps)
is threaded through a per-event `EventContext`.

### Domains

| Domain | Responsibility |
|---|---|
| **Event** | per-event scalars (run/event/fill, energies, beamspot, B field) |
| **Truth** | generator particles (LUND) and exact reco↔gen links |
| **Tracking** | charged-track helix + covariance, particles, impact parameters, TE/extrapolation TrackStates, VD hits, electron-refit tracks, the sDST↔fDST match, and the hybrid particle/vertex re-pointing |
| **Vertex** | primary-vertex chain, beamspot, official V0s, photon conversions |
| **Calorimeter** | EM/HCAL/STIC/combined showers (condensed sDST + per-pad/per-tower fullDST), TOF/HOF hits, and the shower hybrid |
| **Pid** | dE/dx, muon/electron/hadron ID, RICH, π⁰, TOF, and the PID hybrid |

### Shared infrastructure

| Unit | Role |
|---|---|
| `PhdstHarness` | PHDST init + event loop; `(run,event)` matching for pass 2; podio I/O |
| `CollectionWriter` | writer base class + `EventContext`; builds canonical collection names |
| `pawalk` | PA-bank chain walk (`lphpa` / `iphreq` / `forEachPA`) |
| `BankPrefix` | the `<source>_<BANK>_<ReadableName>` naming table |
| `PerigeeMatch` | binds a fullDST PA to its shortDST track by perigee geometry |
| `TeBank` | decodes the variable-length PA.TE* track-element bank |
| `HpcPadDecoder` | unpacks HPC PXHGET pad words |
| `Helix` | track-parameter conversion (below) |

### `Helix` — track-parameter conversion

`delphi_edm4hep::Helix` is the single value type for DELPHI ↔ EDM4hep track
parameters. Named factories convert *into* the canonical EDM4hep helix basis
`(D0, phi, omega, Z0, tanLambda, time)` + 6×6 covariance; accessors convert
*out*:

```cpp
Helix::fromPerigee(d0,z0,theta,phi,1/R, weightMatrix)   // PA.TRAC / PA.ELTR
Helix::fromTrackElement(c1,c2,c3,theta,phi,1/P, invPt, cylindrical, cov, q, B)  // PA.TE* / PA.TRAX
Helix::fromHelix(D0,phi,omega,Z0,tanLambda)
   -> .params() / .cov() / .momentum(B,q) / .toTrackState(location)
```

with `omega = kOmega · q · B · (1/|p_T|)` (the transverse curvature), `kOmega =
2.99792458e-4`. The TE bank momentum word is `1/|p_T|` or `1/|p|` per its descriptor,
so `fromTrackElement` takes an `invPt` flag and divides by `sin(theta)` in the `1/|p|`
case; the perigee path and `momentum()` treat `omega` as curvature too, so all are
consistent. The covariance is a Jacobian push-forward (`J · C · Jᵀ`). The header is public
so analysis code can convert track parameters (and recover momentum from
`omega` given B and charge) without running the converter. Raw bank *parsing*
(`TeBank`, `HpcPadDecoder`) is separate and feeds the factories.

### `delphi_bs_fit`

Standalone tool (pure podio/ROOT) over the converter output. Groups
`sDST_PV_PrimaryVertex` positions by run, takes a Tukey-biweight robust mean
as the beamspot **centre** (uncertainty shrinks as 1/√N), keeps the physical
beam **width** from the per-event beamspot parameters, and writes one line per
run. This re-derives a self-consistent beamspot for data/MC closure; it is a
companion to `sDST_BSP_BeamSpot` and does not replace it. Run it once per
sample (data and MC separately).

---

## 4. Scope

The converter emits DELPHI-original bank values. In-converter algorithms that
synthesise non-bank quantities (alternative vertex finders, a second
particle-flow, empirical covariance rescaling, shower-shape moments) are not
applied here; where a downstream recipe is useful it is provided as a separate
tool (e.g. `delphi_bs_fit`) beside the original collection. Event-level summary
banks without a clean EDM4hep type (jets, trigger, run quality) and a
few rare/forward per-PA modules are not currently emitted.

B-tagging is the one place the converter also *recalculates* rather than only
transcribing (§2.5): AABTAG is rerun at conversion time because the stored tag
describes data and simulation differently. Both are emitted, and the
provenance marks the rerun `Derived` and the stored tag `Transcribed`.
