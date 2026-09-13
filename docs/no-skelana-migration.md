# SKELANA-free converter design

The production `delphi_sdst_pass` and `delphi_fdst_pass` executables no longer
call `PSINI`, `PSBEG`, or any other SKELANA entry point. Both link through the
DELPHI archive group that deliberately omits `libskelanaxx`.

The native Code4hep `DelphiSource` uses these same production pipelines. Its
`delphiRun` launcher also links only the archive group without
`libskelanaxx`; SKELANA is not hidden in the plugin boundary.

SKELANA remains available only as an optional, non-installed validation oracle
when configuring with `-DDELPHI_BUILD_SKELANA_REFERENCE=ON`. Its adapter is a
separate static target and is not part of `libdelphi_edm4hep`.

## Converter-owned event preparation

For every accepted PHDST record, the harness now performs this sequence:

1. `EventInfo` reads the processing tag with `DSTQID`, the DST version and
   centre-of-mass energy from the pilot record, and the field with `BPILOT`.
2. The VD package is initialized once with `VDIDST`. Beam defaults formerly in
   SKELANA's `PSCBSD` deck are supplied to `SETBS`, then `VDBSPT` provides the
   data or simulated event beamspot.
3. The stored BTAG bank is decoded directly. AABTAG recalculation calls
   `AADATA`, `AABTGS`, and `AAHEMI` exactly once with the same converter-owned
   beamspot that is written to EDM4hep.
4. For short DSTs, code-120 secondary hadronic interactions receive the same
   `MAKEMOD8(...,.FALSE.,...)` repair that `PSBEG` performed.
5. Domain writers decode the current PA/PV structures directly and publish
   converter-owned maps keyed by raw PA addresses.

`SETBS` and `MAKEMOD8` pass their arguments through old LOCB/LOCF machinery.
Their C++ call arguments therefore use persistent static storage, matching the
lifetime of the old Fortran DATA/local variables; stack-backed arguments are
rejected by the 64-bit compatibility check.

## Replacements for the SKELANA commons

- Event scalars and beamspot: direct pilot-record, `DSTQID`, `BPILOT`, and VD
  package calls.
- Tracking: direct PA `MAIN`/`TRAC` view, charged-first correspondence map,
  direct `TBDCAE` impact parameters, and C++ ports of `PSPGBM`/`PSRDCA`, track
  selection for charged tracks, primary-vertex refits, and mammoth recovery.
  Primary-vertex
  refits use the standalone DELPHI `CONFPV` and `FKMI5` services.
- Vertices, V0s, and conversions: direct PV/PA structure traversal.
- Simulation truth: direct compact/full truth-structure decoding and raw
  PA-to-simulation links.
- VD hits: direct `MVDH` decoding, grouped by PA rather than VECP slot.
- Particle identification: direct `MUID`, `ELID`, `HAID`, `PHOT`, and related
  PA modules, plus standalone dE/dx and RICH services.
- STIC: direct `STIC`/`SSTC` rows associated by PA. Wrapped SSTC azimuths use
  the shipped `PXCONS.PI` bit pattern, preserving the legacy single-precision
  result rather than substituting the one-ULP-larger C++ value.
- B tagging: direct stored-bank reader and direct AABTAG invocation.

This removes event-to-event dependence on PSC common-block contents. Raw PA
addresses are the stable relation key; transient VECP indices are retained
only where an output compatibility field needs their ordering.

Inside Code4hep, Podio Frame parameters are temporarily materialized as typed
event products because Stitched's product registry transports collections,
not Frame metadata. `PodioOutputModule` reverses that representation before
writing. This keeps the direct `DSTQID`, `BPILOT`, VD beamspot, and BTAG
configuration results as normal Podio parameters in the final file.

## Native scheduled-module migration

`DelphiEventSummaryProducer` is the first conversion calculation scheduled by
Code4hep after `DelphiSource`. The source transcribes the raw PA.MAIN charge
code into `sDST_MAIN_Particles_ChargeCode`; the producer consumes that immutable
collection and independently publishes `native_EVT_nCharged` and
`native_EVT_nNeutral`. The integration test requires those values to match the
legacy event summary. This dual-output pattern is the migration seam for moving
the remaining derived calculations out of the PHDST callback before deleting
the corresponding legacy implementation.

## Native simulation geometry migration

The authoritative DELSIM detector description is not GDML. For v94c,
`SXDDAP` selects detector/date/level records and invokes `DEFGEO` on the
readable CARGO snapshot `CERNSNAP2001_94DELSIM.ASC`. That snapshot contains
7,703 `GEOM` records and 202 `MATC` records; shape, placement, material, and
replacement directives are stored as fields such as `SHAP`, `REFR`, `MATS`,
and `REPL`.

`delphi_geometry` is the first native replacement for that path. It parses the
CARGO records, validity intervals, fields, and continuation data without any
DELPHI or CERNLIB dependency. Its typed model now validates and decodes the
material definitions, two-material assignments, all `SHA*` shape payloads,
six-value `REF*` transforms, twelve-value `DBF MTRX` transforms, and
variable-length `REPL` paths. `MTRX` has the same precedence over `REFR` as in
DDAPP. On the original
v94c snapshot this yields 202 materials, 7,703 geometry nodes, 5,424 material
assignments, 6,220 shapes, 5,095 transforms, and 1,506 replacement directives.
The accepted shapes are DELPHI's documented `BRIK`, `CYL*`, `DUMY`, `FORB`,
`PARA`, `PLNM`, `POL*`, `SPHE`, and `WED4` families; the typed reader rejects
unknown tags and incorrect word counts with source-line diagnostics.

`delphi_geometry_audit` exercises both lossless parsing and typed decoding on
an original snapshot. Subsequent geometry work should translate this model
into DD4hep/GDML one detector subsystem at a time and compare the result
against the DELSIM database hierarchy; it must not substitute an illustrative
detector for the database geometry.

`delphi_geometry_export` renders `/DELF.B` as a GDML cylindrical world using
the snapshot's primary `SHAP` bounds (680 cm radius and 1,170 cm total length)
and its `AIR*` material parameters. With `--beam-pipe`, it also renders the
complete `/BEA*` hierarchy: 106 source nodes, all `CYL1`, `CYL3`, and `BRIK`
components, nested shapes, material assignments, `REFR` placements, and the
`MSK1 -> MSK2` replacement with inherited insert children. DELPHI's `DXMATR`
Euler convention is converted through its rotation matrix instead of treating
the three stored angles as GDML angles. The `VACU` zero-density sentinel is
mapped explicitly to a positive Geant4 transport vacuum of `1e-25 g/cm3`.

The generic writer also supports the complete 81-node `/TPC*` hierarchy.
DELPHI `POL6` endplate sectors are reconstructed from the three radial edges
defined by `DLPOL6` and emitted as closed twelve-vertex tessellated solids;
the two `ARM2` sensing media (`ARC0` and `ARC1`) are tagged as step-preserving
Code4hep tracker-sensitive volumes with DELPHI's `TPCSTP=1` wire-spacing
transport limit: the calibrated `WSPTPC=0.4 cm` makes the effective maximum
step 0.4 cm. The original v94c beam-pipe
plus TPC output is accepted by the Code4hep Geant4 driver at 1.2312434 T and
produces persistent step-level physical tracker hits in the controlled
one-muon transport test.

The `--vertex` mode adds the complete 1,137-node `/VD**` tree. Its 508 `DUMY`
records are emitted as GDML assemblies, so their child topology and `MTRX`
placements survive without inventing material or geometric extent. The 613
physical `BRIK` volumes include all 288 silicon sensors selected by their
authoritative `SI**` material assignment. Those sensors use the VD simulation's
`STEPS=0.001 cm` transport limit and produce persistent, MC-related
`SimTrackerHit`s in the one-muon native Geant4 test.

The `--id` and `--od` modes add the remaining central tracking volumes. The
inner detector's `FORB` cells are reconstructed as closed eight-vertex
tessellated solids, and the outer detector's multi-unit `POL4` rings retain
their hollow inner faces and database-defined segmentation. The authoritative
`GASV` and `LAY1`--`LAY5` volumes are tracker-sensitive. `--tracking` composes
beam pipe, VD, ID, TPC, and OD into one GDML detector: the v94c export contains
2,004 logical volumes, 2,559 placements, and 439 sensitive volumes. A fixed
transverse muon produces persistent, MC-related hits in every central-tracker
region in one Code4hep Geant4 run. Every sensitive volume also receives a
semantic 64-bit cell-ID base: the high byte identifies VD, ID, TPC, or OD, the
next 24 bits identify the rendered sensor, and Code4hep supplies the physical
copy number in the low 32 bits. This replaces the former accidental dependence
on Geant4 hit-collection ordering.

`DelphiTrackerHitPartitionProducer` is the first scheduled consumer of that
contract. It splits the common Code4hep transport collection into ordinary
`VertexSimHits`, `InnerDetectorSimHits`, `TpcSimHits`, and
`OuterDetectorSimHits` products, copying the complete simulated-hit payload and
its `MCParticle` relation. Later response modules can therefore consume their
own subsystem without radius cuts or knowledge of Geant4 collection order.

`VertexDigitizationConditions` removes the first VDSIM COMMON-block boundary.
It now uses the release-matched VDSIM 4.6 source shipped with v94c, rather than
the later VDSIM 6.3 upgrade geometry. The port fixes the 24/24/24-module,
four-plaquette topology; odd/even inner-module P-channel counts; the closer
layer's two-zone N pitch; the outer central/peripheral N layouts; the five
noise zones; five-sigma thresholds; three-step acceptance; 1,000-electron ADC
calibration; 8 micrometre Lorentz shift; two-strip minimum noise cluster; and
the default noise-cluster/cross-talk switches. These values are regression
tested independently before the scheduled VD digitizer consumes them.

`VertexChannelResponse` now supplies the first electronics kernel. It converts
Geant4 silicon energy deposition through the 3.6 eV electron-hole creation
energy, applies the selected layer/side Gaussian noise in electron units, and
reproduces `SVPACK`/`SVFORM`'s 1,000-electron ADC calibration, quarter-ADC
integer encoding, 13-bit signal word, 8-bit noise word, and five-sigma
single-channel diagnostic. Random deviates remain explicit inputs so the
scheduled producer can own deterministic run/event seeding.

`VertexReadoutGeometry` binds those v94c conditions to the authoritative
CARGO geometry. It enumerates all 288 barrel sensors in exactly the same order
used by the persisted semantic transport cell IDs, decodes layer, module,
half-module and physical plaquette, attaches the DBF `MTRX` transformations and
`USER` active-area endpoints for the P plane and the 192 instrumented N planes,
and provides checked local/global transforms. The snapshot audit pins the
known sensor-22 mapping and verifies cell-ID and coordinate round trips for
every sensor. It also ports `SVELCH`'s v94c SIROCCO/channel mapping and checks
all 319,488 physical readout addresses. This is the geometry/electronics
contract needed by the scheduled strip digitizer.

`VertexStripReadout` adds the framework-independent channel-location kernel.
It maps global transport positions to the exact v94c P- or N-side strip,
including module staggering, the Lorentz displacement, half-module
orientation, and the closer layer's two-pitch N-side boundary. It supplies an
unambiguous modern strip cell ID while retaining the separate legacy
SIROCCO/channel mapping for validation. The snapshot audit exhaustively checks
that the measurement centre of every one of the 319,488 instrumented strips
round-trips through both the locator and the cell-ID codec. This remains a
readout primitive rather than a response model.

`DelphiVertexDigitizerProducer` is the first scheduled VD response slice. It
consumes the partitioned `VertexSimHits`, groups step deposits by particle,
sensor, and readout side, enforces VDSIM's three-active-step criterion, maps
each deposit to its v94c strip, adds one deterministic run/event-seeded noise
draw per aggregated channel, applies the five-sigma threshold, and writes
EDM4hep `RawTimeSeries` digits. A transient digit-to-simulated-hit relation
feeds `DelphiVertexHitReconstructionProducer`, which writes persistent
one-dimensional `TrackerHitPlane` measurements at the calibrated strip
positions plus standard EDM4hep truth links. A controlled Code4hep/Geant4
muon run checks the complete scheduled and persistent path. This is not yet
full VDSIM physics: the current deposit goes to its nearest readout strip;
`SVPUL` charge transport and sharing, delta rays, generated noise clusters,
optional cross-talk, and P/N cluster pairing still require native ports and
legacy closure.

`InnerDetectorReadoutGeometry` begins the same replacement for IDSIM. It
decodes all 24 jet sectors with their 24 calibrated drift wires and all five
trigger layers with 192 anode wires and 192 cathode strips per layer. The
native catalogue retains every 21-word jet calibration, two-word trigger
calibration, channel status, wire/strip position, active length, the three
time-zero constants, dead time, cathode/anode charge ratio, and cathode charge
width used by v94c `SIGEOM`/`SICALB`. The snapshot audit currently finds two
disabled jet channels and no disabled trigger channels. The corresponding
`SIDA`/`SIDC` anode-wire and cathode-strip locators round-trip every one of the
1,920 trigger addresses.

`InnerDetectorJetResponse` removes IDSIM's first calibrated response boundary.
It ports `SICALB`'s temperature/pressure and per-sector high-voltage
corrections, magnetic-field scaling of the Lorentz angle, bad fence-voltage
repair (including the historical sector-17 override), and `SICALR`/`SIFTOT`'s
two-region phi-to-drift-time transform. Its `SITTOF` inverse preserves the
legacy drift-gap clamp, while `SITTOC`/`SICTOT` provide the calibrated
four-bin fine-TDC conversion and a defined near-wire quantization clamp. The
response is exercised across every sector and wire. At
1.2312434 T the v94c snapshot audit pins the Lorentz angle at -6.36012 degrees
and the maximum jet drift time at 1940.97 ns.

`DelphiInnerDetectorDigitizerProducer` schedules that response after the
central-tracker partition. It reconstructs every calibrated wire-cylinder
crossing from the Geant4 segment, applies deterministic run/event-seeded r-phi
resolution and the v94c wire status, 80 percent efficiency, and 55 ns
dead-time, then stores the 14-bit TDC word in an EDM4hep `RawTimeSeries` for
the physical sector/wire channel. The physical digit deliberately has no
left/right bit because the jet chamber did not measure it. A transient raw
digit truth relation feeds `DelphiInnerDetectorHitReconstructionProducer`,
which inverts the TDC and writes each valid left and right
`TrackerHitPlane` hypothesis plus persistent standard EDM4hep truth links.
CI checks all channel encodings, measurement geometry, provenance, and exact
fixed-seed replay after ROOT readback. Jet charge induction and noise, trigger
anode/cathode response, ambiguity resolution by tracking, and quantitative
IDSIM closure remain.

`OuterDetectorReadoutGeometry` removes the first ODSIM geometry and calibration
COMMON-block boundary. It reads the 436-word measured `FIDS` survey, reproduces
`SOGEOM`'s five staggered layers and 24 plank transformations, and catalogues
all 3,480 real tubes. Per-tube pedestal, z-propagation delay, pulse width, and
efficiency come directly from the 24 `CALW` records; the quadratic 300
micrometre wire sag and the physical column gaps are retained. The snapshot
audit checks every wire-centre locator and physical cell-ID round trip.

`OuterDetectorDriftResponse` is a direct C++ port of `SODSTM`: the four
fifth-order laser-data curves, 0/30/45/60/90-degree interpolation, 0.4 mm
avalanche-region clamp, and numerical inverse are independently tested.
`DelphiOuterDetectorDigitizerProducer` locates each partitioned Geant4 step in
the surveyed tube catalogue, applies deterministic 100 micrometre transverse
and 5.49 cm longitudinal smearing plus calibrated efficiency, keeps the
earliest response per tube, and includes pedestal, z propagation, and time of
flight in the raw leading time. A versioned physical-channel payload retains
the calibrated drift time, z measurement, incidence angle, and pulse width;
it is intentionally not presented as the historical multiplexed crate/TDC
word. `DelphiOuterDetectorHitReconstructionProducer` inverts it into both
left/right `TrackerHitPlane` hypotheses and persistent standard truth links.
CI checks a real scheduled Geant4 event and exact fixed-seed replay. Porting
`SORCVR`/`SOMPLX`/`SOTDC`, accidental noise, track-segment common-perpendicular
refinement, and quantitative ODSIM closure remain before the OD response can
be called fully legacy-equivalent.

`TpcReadoutGeometry` is the first native digitization service. It reads the 16
pad-row `LOCC`/`SIZC` calibration records and all 12 measured sector transforms
from that snapshot. Its pad locator reproduces `STAMPA`'s one-centimetre row
window, 60-degree sector coordinates, Fortran truncation, and asymmetric
zero-angle pad boundary. For v94c the audit requires 1,680 pads per sector,
20,160 pads in total, and a successful centre-pad round trip through every
aligned sector transform.

`DelphiTpcPadMapperProducer` schedules that service after `G4SimProducer`. It
consumes step-level `SimTrackerHit` objects, applies the calibrated sector and
row window, and publishes persistent `TrackerHit3D` pad hits with an explicit
endcap/sector/row/pad cell ID. This establishes the deterministic simulation to
readout boundary.

`TpcPadResponse` is the next deliberately framework-independent layer. It
reproduces STAMPA's deterministic induction onto the central pad and its two
neighbors on either side. The response width uses the v94c `STSPRF` constants,
the measured row pitch, drift distance, local track incidence, and DELPHI's
Lorentz-angle term. The drift half-length is read from the selected sector
geometry rather than duplicated as steering configuration. The returned signal
retains caller-defined units; the scheduled digitizer owns the later conversion
from Geant4 energy deposition through ionization, avalanche fluctuation, drift,
shaping, calibrated FADC response, and threshold selection.

`TpcDigitizationConditions` now decodes the corresponding CARGO calibration
records without the legacy database runtime. It reads the global high voltage,
minimum-ionizing dE/dx and mean-pad-amplitude normalizations, both endcap drift
velocities, and the packed two-bit gate state for every physical sector. The
v94c audit fixes these values at 25,306 V, 254.5, 652.8, 6.998 cm/us, and
7.002 cm/us. It also decodes all 20,160 packed per-pad `CALP` records into
pedestal, low/high range slopes, gain ratio, crossover signal, electronics
channel, and status. This preserves STCALB's row-to-crate permutation, closed-
gate correction, and historical 0.494-to-4.94 database repair. The v94c audit
finds 736 nonzero pad statuses and a gain-ratio range of 4.052--5.286.

`TpcTimeResponse` ports the deterministic part of STDIPW: longitudinal
diffusion, track-step broadening, electronics shaping, the 73.82 ns clock,
13-bin sampling window, baseline term, and asymmetric pulse shape. Its two
truncated-Gaussian inputs are explicit arguments, so a scheduled digitizer can
own and seed the random stream without hiding global Fortran RNG state.

`TpcFadc` ports STFADC's two-range calibrated conversion, common and
per-sample pedestal fluctuations, integer truncation, and 8-bit saturation.
Its zero-suppression step preserves STODIG's 20-count pad threshold, two past
samples, two future samples, five-below-sample closure, and 20-cluster limit.
As with the pulse shaper, Gaussian draws are inputs rather than hidden global
state.

`DelphiTpcDigitizerProducer` connects these services as a scheduled Code4hep
module. It converts Geant4 energy deposition using STDEDX's 20 eV/electron and
STLAND's 0.016 avalanche scale, applies Fano and avalanche fluctuations from a
run/event-derived local seed, aggregates every contribution by pad and time
bin, calibrates and zero-suppresses the result, and publishes surviving EDM4hep
`TimeSeries` waveforms. Two repeated controlled runs produce bit-identical
waveforms. Geant4 already supplies energy-loss fluctuations, so this path does
not also sample the legacy ETDEDX histogram. Native wire assignment and the
STDEDX/STLAND adjacent-wire leakage are present. The digitizer retains
charge-weighted `SimTrackerHit` contributions in memory; the reconstruction
module publishes them as standard `TrackerHitSimTrackerHitLink` objects whose
weights are normalized per hit. Exact physics closure still needs comparison
against DELSIM's track labels.

`DelphiTpcHitReconstructionProducer` is the first native reconstruction module
on that output. For each zero-suppressed waveform it finds the peak sample,
converts drift time to z with the appropriate measured endcap velocity, places
the hit at the calibrated pad centre, propagates pad and time-bin dimensions to
a position covariance, and carries the channel status into hit quality. It
publishes ordinary EDM4hep `TrackerHit3D` objects for the later pattern-
recognition and track-fit stages; ADC amplitude is deliberately not mislabeled
as an energy deposit.

`CentralTrackFit`, `CentralTrackFinder`, and
`DelphiCentralTrackFitProducer` establish the native central pattern-recognition
and fit boundary. The scheduled producer clusters contiguous TPC pads within a
row, while the framework-independent finder builds deterministic circle
hypotheses, selects at most one cluster per physical row across all sectors,
fits transverse and longitudinal helix parameters, and removes claimed
clusters before finding the next candidate. It therefore emits multiple
exclusive candidates and permits a candidate to cross sector boundaries. Each
ordinary EDM4hep `Track` carries its selected raw TPC hit relations, perigee
parameters `(D0, phi, omega, Z0, tanLambda)`, fit quality, hole count, and finite
covariance estimates. Finding is based solely on reconstructed hits; truth is
not consulted. The checked synthetic contract contains two tracks, each
crossing a sector boundary, plus unrelated clusters.
`HelixTrajectory` and `DelphiCentralTrackExtensionProducer` add the next native
tracking boundary. They propagate each seed through reconstructed detector
coordinates, assign every compatible VD measurement to its closest candidate,
and select one helix and one drift side globally for each ID/OD physical
channel. The resulting ordinary EDM4hep `Track` preserves the seed state and
TPC relations while adding its selected VD, ID, and OD relations. The fixed
10 GeV muon CI event currently retains 6 VD, 18 ID, 47 TPC, and 4 OD hits and
repeats exactly.

`fitCentralTrackMeasurements` and `DelphiCentralTrackRefitProducer` perform a
weighted geometric refit after that selection. They retain the partial
coordinate contract of the planar detectors: VD and ID contribute transverse
constraints without allowing their unmeasured longitudinal coordinate to bias
`Z0` or `tanLambda`, while TPC row centroids and OD provide both coordinates.
The geometric circle iteration reduces the high-momentum bias of the stable
algebraic seed; in the fixed CI event it moves the reconstructed transverse
momentum from 7.85 GeV to 8.85 GeV for a 10 GeV input without changing hit
ownership.

This remains an initial global fit and multi-candidate finder. Its pair-seeded
search is intended to establish the native data and ownership contract, not yet
the final dense-event algorithm. Beamspot rather than origin constraints,
robust outlier rejection, material effects, displaced-track seeding, scalable
high-occupancy tuning, and quantitative DELPHI tracking closure remain.

`DelphiTrackTruthProducer` closes the native tracking provenance chain. It
joins the selected hit relations back to the standard VD, ID, TPC, and OD
`TrackerHitSimTrackerHitLink` products, accumulates their normalized
contributions by persistent MC-particle identity, and emits standard
`TrackMCParticleLink` weights normalized per reconstructed track. It never
uses truth in finding, ambiguity selection, or fitting. The controlled muon
has one dominant relation of weight 1; this product is the validation contract
for efficiency, purity, and fake-rate measurements once multi-track finding is
introduced.

The snapshot path is retained as GDML auxiliary provenance. All modes reject a
missing or structurally different hierarchy instead of silently falling back.
The central tracker is now transported and all four barrel tracking systems
have scheduled response/reconstruction seams. The TPC has native calibrated
digitization and hit reconstruction; the VD has its first strip and planar-hit
path; the ID jet chamber has raw TDC digits and left/right hypotheses; and the
OD has surveyed physical-tube digits and left/right three-dimensional planar
hypotheses. Full VD/ID/OD response fidelity, the ID trigger layers,
production-quality dense-event pattern recognition, spatial magnetic-field
mapping, calorimeter and muon geometry/response, and their reconstruction
remain explicit migration slices.

The generic Code4hep Geant4 driver now requires `magneticFieldTesla` in its
detector configuration instead of hiding a 0.1 T value in C++. It persists
that value as the `sim_detector_magneticFieldTesla` Frame parameter. For the
v94c DELSIM default, `CURRX=5001` A maps through `UFCSCL` to 1.2312434 T at the
centre. This uniform value is only the first conditions seam: faithful
simulation still requires a native port of the spatial UFIELD map.

## Validation findings

On the five-event 94C2 simulation fixture, the direct sDST output agrees with
the migration baseline exactly for particle four-vectors, tracks, QTRAC impact
parameters, detector/reconstruction metadata, vertices, generated truth, VD
hits, standard PID, PID extras, calorimeter objects, AABTAG, and their normal
relations. A freshly built optional PSBEG oracle and the production executable
produce identical detailed `podio-dump` output for all five events.

Several differences from older common-block-backed dumps are intentional
corrections rather than lost information:

- legacy `IPAST` can retain duplicate reconstructed-to-truth associations
  that cannot be produced by the current event's raw PA/ST mapping; the direct
  relation contains only raw-supported links;
- legacy RICH fields can survive in HAID slots whose current PA has no matching
  gas/liquid descriptor; direct decoding leaves those absent fields empty;
- MTPC word 10 is a bit field, including a package-private random marker, not
  decimal packing; the direct decoder extracts its documented low bytes;
- two old LVLOCK bit-1 values in the fixture are not supported by any current
  track cut and repeat the same slot's preceding-event value; the direct flag
  is recomputed from the current PA only.

For full-DST simulation, removing the rest of `PSBEG` also removes unrelated
random-number consumption between events. The first event agrees with the
whole-PSBEG oracle; later simulated beamspot draws can differ because the old
global DELPHI random stream has advanced by a different amount. Within the
direct converter, each event uses one internally consistent beamspot for VD,
AABTAG, and EDM4hep output, and repeated direct runs are deterministic.

## Build and link checks

The default build keeps `DELPHI_BUILD_SKELANA_REFERENCE=OFF`. Validation should
check both production link files for absence of `skelanaxx`, scan the binaries
for `psini_`/`psbeg_`/`pshort_`, run CTest, and exercise representative data
and simulation conversions. Reference targets may be enabled for A/B studies,
but they are never installed.
