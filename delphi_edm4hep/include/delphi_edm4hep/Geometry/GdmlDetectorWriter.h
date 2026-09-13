#pragma once

#include "delphi_edm4hep/Geometry/GeometryModel.h"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace delphi_edm4hep::geometry {

struct GdmlVolumeAnnotation {
  std::string path;
  // Empty for a passive volume; otherwise a Code4hep SensDet value.
  std::string sensitiveDetector;
  double maximumStepCm{};
  // Optional subsystem byte. The writer assigns a stable sensor ordinal and
  // reserves the low 32 bits for Geant4's physical copy number.
  std::uint8_t cellIDSubsystem{};
};

struct GdmlDetectorRoot {
  std::string path;
  // Empty for passive roots; otherwise a Code4hep SensDet value.
  std::string sensitiveDetector;
  double maximumStepCm{};
  // Apply transport properties to specific descendants without rendering
  // them as duplicate top-level trees.
  std::vector<GdmlVolumeAnnotation> descendants;
};

// Write one or more top-level DELPHI detector trees into the authoritative
// world. This is the growing native detector-construction seam; only shape
// families with validated DELPHI-to-GDML translations are accepted.
void writeGdmlDetector(std::ostream &output, const GeometryModel &model,
                       const std::vector<GdmlDetectorRoot> &roots,
                       std::string_view worldPath = "/DELF.B",
                       std::string_view snapshotIdentifier = {});

} // namespace delphi_edm4hep::geometry
