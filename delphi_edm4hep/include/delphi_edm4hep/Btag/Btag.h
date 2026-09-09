// Btag.h — b-tagging domain writer.
//
// Emits both forms of DELPHI's b-tag:
//
//   <prefix>_BTG_*     the tag stored on the DST. Transcribed; NaN where the
//                      bank was not written.
//   <prefix>_AABTAG_*  the tag recalculated here. Derived, and the only form
//                      carrying per-track quantities and a primary vertex.
//
// Per-track rows are in AABTAG's own ordering (1..NTRK), each linked to its
// particle. Impact parameters ride on the track as a TrackState at AtVertex
// (see Tracking.cpp), not here.

#pragma once

#include "delphi_edm4hep/CollectionWriter.h"

#include <string_view>
#include <unordered_map>

namespace delphi_edm4hep::btag {

class BtagWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;

  void emit() override;

private:
  // Emit the event and hemisphere b-tag probabilities plus the thrust that
  // are currently in the PSCBTG common, under `bank`. Both the stored tag and
  // the recalculated one land there, one after the other, so the caller
  // controls which is being read.
  void emitEventLevel(std::string_view bank, Provenance prov, bool valid);

  // Run DELPHI's combined tag (AACMBT / AACMZ0, DELPHI 97-094) on the commons
  // AABTGS left behind and emit, under AABTAG_*: the secondary-vertex
  // hypotheses of AAFSEC, AABTAG's jets with their per-jet tag variables,
  // and the event-level combined tag. `lpa_to_pa` resolves AABTAG's track
  // addresses to particles. Emits empty collections / NaN when `valid` is
  // false, without calling into AABTAG.
  void emitCombinedTag(bool valid,
                       const std::unordered_map<int, int>& lpa_to_pa);
};

}  // namespace delphi_edm4hep::btag
