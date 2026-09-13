// Vertex domain: LDTOP-1 chain + V0ID (PSCRV0) + photon conversions
// (PSCPHC) + beam spot (direct VD package result). VertexWriter reads ctx_.tracking
// for the V0 / PhotonConv -> Particle relations.

#pragma once

#include "delphi_edm4hep/CollectionWriter.h"

namespace delphi_edm4hep::vertex {

class VertexWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;
  void emit() override;
};

}  // namespace delphi_edm4hep::vertex
