#pragma once

#include "delphi_edm4hep/Geometry/GeometryModel.h"

#include <iosfwd>
#include <string_view>

namespace delphi_edm4hep::geometry {

// Write the authoritative DELPHI world plus the complete /BEA* beam-pipe
// hierarchy as GDML. Replacement nodes are expanded into independent logical
// volumes so their inherited children keep the placement of each instance.
void writeGdmlBeamPipe(std::ostream &output, const GeometryModel &model,
                       std::string_view worldPath = "/DELF.B",
                       std::string_view beamPipePath = "/BEA*.B",
                       std::string_view snapshotIdentifier = {});

} // namespace delphi_edm4hep::geometry
