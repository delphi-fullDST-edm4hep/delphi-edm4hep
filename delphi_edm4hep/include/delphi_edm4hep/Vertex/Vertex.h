// Vertex domain: LDTOP-1 chain + V0ID (PSCRV0) + photon conversions
// (PSCPHC) + beam spot (PSCBSP). VertexWriter reads ctx_.tracking
// for the V0 / PhotonConv -> Particle relations.

#pragma once

#include "delphi_edm4hep/CollectionWriter.h"

#include <cstdint>

namespace delphi_edm4hep::vertex {

/// Whether `PV_Tracks_d0PV` / `_z0PV` hold a measurement for this track.
///
/// Those two are computed here from the track's `AtIP` perigee and the primary
/// vertex, because SKELANA's `QTRAC(38,39)` are unfilled on real data. Where
/// that is not possible both read `-999`, which is a sentinel and not a
/// position; this flag is the reliable test.
///
/// @collection{PV_Tracks_ImpactFlag}
enum ImpactFlag : std::int32_t {
  kImpactMissing = 0,  ///< no primary vertex, or the track has no AtIP state:
                       ///< d0PV and z0PV are -999
  kImpactValid   = 1   ///< d0PV and z0PV are measured, in mm, LCIO sign
};

class VertexWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;
  void emit() override;
};

}  // namespace delphi_edm4hep::vertex
