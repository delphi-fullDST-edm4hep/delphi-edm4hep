// TruthData.h — value types passed through EventContext.
// Separate from Truth.h so CollectionWriter.h (which holds EventContext)
// can include this without pulling in the writer classes (avoids
// include cycle, since the writers inherit from CollectionWriter).

#pragma once

#include <edm4hep/MutableMCParticle.h>

#include <vector>

namespace delphi_edm4hep::truth {

// Output of TruthGenWriter::emit(): per-LU-index MutableMCParticle
// handles (0-indexed; handles[i] ↔ LU(i+1) in PSCLUJ). Handles remain
// valid after the collection has been moved into the Frame because
// podio object handles wrap a stable pointer into per-collection
// storage. Consumed by TruthRecoLinkWriter (uses PSCTBL exact tables
// to bind each handle to a tracking::Output Particle handle).
struct GenParticleResult {
  std::vector<edm4hep::MutableMCParticle> handles;
  // MCParticle of each simulated track, by track number (entry 0 unused):
  // its generator line's particle, or the particle DELSIM created.
  std::vector<edm4hep::MutableMCParticle> particleOfSimulatedTrack;
};

}  // namespace delphi_edm4hep::truth
