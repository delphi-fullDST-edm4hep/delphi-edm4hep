#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GdmlBeamPipeWriter.h"
#include "delphi_edm4hep/Geometry/GdmlDetectorWriter.h"
#include "delphi_edm4hep/Geometry/GdmlWorldWriter.h"
#include "delphi_edm4hep/Geometry/GeometryModel.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<delphi_edm4hep::geometry::GdmlVolumeAnnotation>
vertexSensitiveVolumes(const delphi_edm4hep::geometry::GeometryModel &model) {
  std::vector<delphi_edm4hep::geometry::GdmlVolumeAnnotation> annotations;
  for (const auto &node : model.nodes()) {
    if (!node.path.starts_with("/VD**/")) {
      continue;
    }
    const auto *definition = model.shapeDefinition(node);
    const auto &materials =
        node.materials.empty() ? definition->materials : node.materials;
    if (!materials.empty() && materials.front().inner == "SI**") {
      annotations.push_back({node.path, "step_tracker_sd", 0.001, 1});
    }
  }
  return annotations;
}

std::vector<delphi_edm4hep::geometry::GdmlVolumeAnnotation>
namedSensitiveVolumes(const delphi_edm4hep::geometry::GeometryModel &model,
                      std::string_view prefix,
                      const std::vector<std::string_view> &names,
                      std::uint8_t subsystem) {
  std::vector<delphi_edm4hep::geometry::GdmlVolumeAnnotation> annotations;
  for (const auto &node : model.nodes()) {
    if (node.path.starts_with(prefix) &&
        std::find(names.begin(), names.end(), node.name) != names.end()) {
      annotations.push_back({node.path, "step_tracker_sd", 0.0, subsystem});
    }
  }
  return annotations;
}

delphi_edm4hep::geometry::GdmlDetectorRoot tpcRoot() {
  return {"/TPC*.B",
          {},
          0.0,
          {{"/TPC*/ARC0.B", "step_tracker_sd", 0.4, 3},
           {"/TPC*/ARC1.B", "step_tracker_sd", 0.4, 3}}};
}

delphi_edm4hep::geometry::GdmlDetectorRoot
vertexRoot(const delphi_edm4hep::geometry::GeometryModel &model) {
  return {"/VD**.B", {}, 0.0, vertexSensitiveVolumes(model)};
}

delphi_edm4hep::geometry::GdmlDetectorRoot
innerDetectorRoot(const delphi_edm4hep::geometry::GeometryModel &model) {
  return {
      "/ID**.B", {}, 0.0, namedSensitiveVolumes(model, "/ID**/", {"GASV"}, 2)};
}

delphi_edm4hep::geometry::GdmlDetectorRoot
outerDetectorRoot(const delphi_edm4hep::geometry::GeometryModel &model) {
  return {"/OD**.B",
          {},
          0.0,
          namedSensitiveVolumes(model, "/OD**/",
                                {"LAY1", "LAY2", "LAY3", "LAY4", "LAY5"}, 4)};
}

} // namespace

int main(int argc, char **argv) {
  const auto mode = argc == 4 ? std::string_view(argv[1]) : std::string_view{};
  const auto detectorMode = mode == "--beam-pipe" || mode == "--tpc" ||
                            mode == "--vertex" || mode == "--id" ||
                            mode == "--od" || mode == "--tracking";
  if (argc != 3 && !detectorMode) {
    std::cerr << "usage: " << argv[0]
              << " [--beam-pipe|--tpc|--vertex|--id|--od|--tracking] "
                 "CERNSNAP*_DELSIM.ASC "
                 "delphi.gdml\n";
    return 2;
  }
  const auto inputIndex = detectorMode ? 2 : 1;
  const auto outputIndex = detectorMode ? 3 : 2;
  try {
    const auto database =
        delphi_edm4hep::geometry::CargoDatabase::readFile(argv[inputIndex]);
    const auto model = delphi_edm4hep::geometry::GeometryModel::fromCargo(
        database, argv[inputIndex]);
    std::ofstream output(argv[outputIndex]);
    if (!output) {
      throw std::runtime_error(std::string("cannot create GDML file: ") +
                               argv[outputIndex]);
    }
    if (mode == "--beam-pipe") {
      delphi_edm4hep::geometry::writeGdmlBeamPipe(output, model, "/DELF.B",
                                                  "/BEA*.B", argv[inputIndex]);
    } else if (mode == "--tpc") {
      delphi_edm4hep::geometry::writeGdmlDetector(
          output, model, {{"/BEA*.B", {}, 0.0, {}}, tpcRoot()}, "/DELF.B",
          argv[inputIndex]);
    } else if (mode == "--vertex") {
      delphi_edm4hep::geometry::writeGdmlDetector(
          output, model, {{"/BEA*.B", {}, 0.0, {}}, vertexRoot(model)},
          "/DELF.B", argv[inputIndex]);
    } else if (mode == "--id") {
      delphi_edm4hep::geometry::writeGdmlDetector(
          output, model, {{"/BEA*.B", {}, 0.0, {}}, innerDetectorRoot(model)},
          "/DELF.B", argv[inputIndex]);
    } else if (mode == "--od") {
      delphi_edm4hep::geometry::writeGdmlDetector(
          output, model, {{"/BEA*.B", {}, 0.0, {}}, outerDetectorRoot(model)},
          "/DELF.B", argv[inputIndex]);
    } else if (mode == "--tracking") {
      delphi_edm4hep::geometry::writeGdmlDetector(output, model,
                                                  {{"/BEA*.B", {}, 0.0, {}},
                                                   vertexRoot(model),
                                                   innerDetectorRoot(model),
                                                   tpcRoot(),
                                                   outerDetectorRoot(model)},
                                                  "/DELF.B", argv[inputIndex]);
    } else {
      delphi_edm4hep::geometry::writeGdmlWorld(output, model, "/DELF.B",
                                               argv[inputIndex]);
    }
  } catch (const std::exception &error) {
    std::cerr << "delphi_geometry_export: " << error.what() << '\n';
    return 1;
  }
}
