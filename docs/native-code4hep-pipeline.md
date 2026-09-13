# Native DELPHI pipeline in Code4hep

## Design boundary

The full rewrite is feasible, but “entirely in Code4hep” should describe the
execution architecture, not put DELPHI detector physics into the framework
core. Code4hep owns configuration, scheduling, event setup, deterministic
random streams, product provenance, concurrency, and EDM4hep I/O. A native C++
DELPHI library owns snapshot decoding, detector geometry, conditions, response,
pattern recognition, calibration, and legacy-compatible algorithms. Thin
scheduled producers connect the two.

No native producer may depend on ZEBRA banks, CERNLIB, PHDST callbacks,
SKELANA lifecycle routines, or mutable Fortran COMMON blocks. Legacy programs
remain validation oracles until each replacement reaches physics closure.

## Replacement map

| Pipeline stage | Native implementation now | Remaining replacement |
|---|---|---|
| Configuration and execution | `delphiRun`, checked-in Python steering, Code4hep scheduler | Remove the legacy-linked launcher after native input no longer needs PHDST |
| Event provenance and conditions seams | processing tag, beamspot, uniform magnetic field in EDM4hep/frame metadata | run-dependent conditions service and spatial UFIELD map |
| Primary generation | Code4hep generator products | campaign-specific generator steering and full validation |
| Detector geometry | CARGO parser and exact GDML for beam pipe, VD, ID, TPC, OD | remaining tracking structures, calorimeters, RICH, TOF, muon system, forward detectors |
| Particle transport | Code4hep Geant4 with persistent truth-linked hits and semantic cell IDs | detector-specific sensitive actions where step hits are insufficient |
| VD | release-matched v94c conditions, 288-sensor readout, scheduled strip digitization, `RawTimeSeries` digits, planar hits, truth links | faithful `SVPUL` charge sharing and delta rays, noise-cluster generation, cross-talk option, cluster pairing and legacy closure |
| ID | authoritative sensitive geometry, transport hits, v94c jet/anode/cathode readout catalogue, calibrated jet drift/TDC response, scheduled raw digits, left/right hit hypotheses, truth links, and helix-based ambiguity selection | trigger-layer digitization, jet charge/noise response, refit feedback, and legacy closure |
| TPC | calibrated readout geometry, wire/pad/time/FADC response, scheduled digitization and hit reconstruction | closure tuning and run-dependent conditions |
| OD | authoritative 3,480-tube surveyed readout and calibration, exact drift-time kernel, scheduled physical-channel digits, left/right planar hits, truth links, and helix-based ambiguity selection | legacy crate/multiplexer/TDC word packing, noise, refit-based angle refinement, and quantitative closure |
| Central tracking | scheduled TPC clustering, deterministic multi-candidate IP-constrained pattern recognition across sector boundaries, VD association, exclusive ID/OD drift-side selection, weighted partial-coordinate global refit, and normalized track-to-MC attribution | dense-event pattern-recognition tuning, outlier rejection, material effects, unconstrained/beamspot fits |
| Vertexing and beamspot | legacy event decoding and standalone beamspot fit | native primary/secondary vertexing and run-level beamspot feedback |
| Calorimetry and muons | legacy conversion only | geometry, transport response, digitization, clustering, calibration, truth |
| PID and particle flow | legacy conversion only | native dE/dx, RICH, TOF, lepton/photon/hadron ID and combined particles |
| Flavour tagging | AABTAG can be recalculated in the transitional path | native tagger or an explicitly isolated compatibility algorithm |
| Analysis output | EDM4hep collections and metadata | stable native event model, validation contract, removal of transitional bank-derived collections |

## Migration rule

Each vertical slice is complete only when it has authoritative geometry and
conditions, a scheduled producer, persistent EDM4hep output, truth relations,
deterministic replay, unit tests, a real-event or real-simulation CI smoke test,
and quantitative closure against the legacy oracle. The legacy implementation
can then be removed for that slice without waiting for the whole detector.

The critical path is VD strip reconstruction, ID/OD hit reconstruction,
central track finding and fitting, then vertexing. Calorimeters, PID and
event-level reconstruction can proceed as independent slices once their
geometry roots and product contracts are fixed.
