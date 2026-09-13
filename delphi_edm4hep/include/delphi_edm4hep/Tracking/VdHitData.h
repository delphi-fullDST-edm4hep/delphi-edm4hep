// VdHitData.h — value type passed through EventContext.
// Separate from VdHits.h so CollectionWriter.h can include this without
// pulling in the writer class.

#pragma once

#include <edm4hep/TrackerHit3D.h>

#include <unordered_map>
#include <vector>

namespace delphi_edm4hep::vd_hits {

// Output of VdHitsWriter::emit(): associated Vertex-Detector hits grouped by
// the raw PA reference stored in the compact hit bank. The track writer uses
// that stable address to attach the hits while its Track is still mutable.
struct Output {
  std::unordered_map<int, std::vector<edm4hep::TrackerHit3D>> lpa_to_hits;
};

}  // namespace delphi_edm4hep::vd_hits
