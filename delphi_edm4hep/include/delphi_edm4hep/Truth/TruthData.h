// TruthData.h — value types passed through EventContext.
// Separate from Truth.h so CollectionWriter.h (which holds EventContext)
// can include this without pulling in the writer classes (avoids
// include cycle, since the writers inherit from CollectionWriter).

#pragma once

#include <edm4hep/MutableMCParticle.h>

#include <unordered_map>
#include <vector>

namespace delphi_edm4hep::truth {

// Output of TruthGenWriter::emit(): per-LU-index MutableMCParticle
// handles (0-indexed; handles[i] ↔ the direct LU-like row i+1). Handles remain
// valid after the collection has been moved into the Frame because
// podio object handles wrap a stable pointer into per-collection
// storage. The raw PA-address map is the direct equivalent of the
// PSCTBL IPAST -> ISTLU correspondence and is consumed by
// TruthRecoLinkWriter.
struct GenParticleResult {
  std::vector<edm4hep::MutableMCParticle> handles;
  std::unordered_map<int, int> lpa_to_gen;
};

}  // namespace delphi_edm4hep::truth
