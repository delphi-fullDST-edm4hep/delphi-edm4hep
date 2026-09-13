// TrackingData.h — value type passed through EventContext.
// Separate from Tracking.h so CollectionWriter.h can include this
// without pulling in the writer class (avoids include cycle since
// TrackingWriter inherits from CollectionWriter).

#pragma once

#include <edm4hep/MutableReconstructedParticle.h>

#include <unordered_map>
#include <vector>

namespace delphi_edm4hep::tracking {

// Output of TrackingWriter::emit():
//   - particle_handles: one MutableReconstructedParticle handle per
//     emitted Particle, in PA-walk order. Handles remain valid after
//     the collection has been moved into the Frame.
//   - vecp_to_particle: VECP index (1..NVECP, with sk::LVPART=1) ->
//     0-based index into the emitted Particle collection. -1 if no
//     Particle was emitted for that VECP entry. Index 0 unused.
//   - pa_to_particle:   PA-walk index (0-based) -> 0-based Particle
//     collection index, or -1 if Tracking dropped the PA.
struct Output {
  std::vector<edm4hep::MutableReconstructedParticle> particle_handles;
  // Raw PA address parallel to particle_handles. It lets downstream direct
  // decoders preserve the converter's particle/VECP ordering without a
  // SKELANA correspondence common.
  std::vector<int> particle_lpas;
  std::vector<int> vecp_to_particle;
  std::vector<int> pa_to_particle;
  // Raw ZEBRA PA address -> particle collection index. Direct decoders use
  // this for banks whose reference links point at PA records (V0, photon
  // conversions, and simulation correspondence tables).
  std::unordered_map<int, int> lpa_to_particle;
};

}  // namespace delphi_edm4hep::tracking
