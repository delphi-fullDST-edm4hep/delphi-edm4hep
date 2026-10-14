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

#include "delphi_edm4hep/Btag/BtagInfo.h"
#include "delphi_edm4hep/CollectionWriter.h"

#include <string_view>

namespace delphi_edm4hep::btag {

class BtagWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;

  void emit() override;

private:
  void emitEventLevel(std::string_view bank, Provenance prov, bool valid,
                      const EventLevelTag& tag);
};

}  // namespace delphi_edm4hep::btag
