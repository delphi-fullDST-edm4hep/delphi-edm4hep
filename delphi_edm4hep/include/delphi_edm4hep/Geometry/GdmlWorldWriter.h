#pragma once

#include "delphi_edm4hep/Geometry/GeometryModel.h"

#include <iosfwd>
#include <string_view>

namespace delphi_edm4hep::geometry {

// Write the authoritative DELPHI world boundary and material as GDML. This is
// deliberately narrower than a detector exporter: child volumes are added
// only after their shape and replacement semantics have validation coverage.
void writeGdmlWorld(std::ostream &output, const GeometryModel &model,
                    std::string_view worldPath = "/DELF.B",
                    std::string_view snapshotIdentifier = {});

} // namespace delphi_edm4hep::geometry
