#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GeometryModel.h"

#include <exception>
#include <iostream>
#include <map>
#include <string>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " CERNSNAP*_DELSIM.ASC\n";
    return 2;
  }

  try {
    const auto database =
        delphi_edm4hep::geometry::CargoDatabase::readFile(argv[1]);
    const auto model =
        delphi_edm4hep::geometry::GeometryModel::fromCargo(database, argv[1]);
    std::map<std::string, std::size_t> kinds;
    std::size_t shapes = 0;
    std::size_t materials = 0;
    for (const auto &record : database.records()) {
      ++kinds[record.kind];
      if (record.kind == "GEOM" && record.findField("SHAP") != nullptr) {
        ++shapes;
      }
      if (record.kind == "MATC" && record.findField("MATF") != nullptr) {
        ++materials;
      }
    }

    std::cout << "records=" << database.records().size() << '\n';
    for (const auto &[kind, count] : kinds) {
      std::cout << kind << '=' << count << '\n';
    }
    std::cout << "GEOM_with_SHAP=" << shapes << '\n';
    std::cout << "MATC_with_MATF=" << materials << '\n';
    std::map<std::string, std::size_t> shapeKinds;
    std::size_t materialAssignments = 0;
    std::size_t references = 0;
    std::size_t replacements = 0;
    std::size_t typedShapes = 0;
    for (const auto &node : model.nodes()) {
      materialAssignments += node.materials.size();
      references += node.references.size();
      replacements += node.replacements.size();
      for (const auto &shape : node.shapes) {
        ++typedShapes;
        ++shapeKinds[std::string(
            delphi_edm4hep::geometry::shapeKindName(shape.kind))];
      }
    }
    std::cout << "typed_materials=" << model.materials().size() << '\n';
    std::cout << "typed_geometry_nodes=" << model.nodes().size() << '\n';
    std::cout << "typed_material_assignments=" << materialAssignments << '\n';
    std::cout << "typed_shapes=" << typedShapes << '\n';
    std::cout << "typed_references=" << references << '\n';
    std::cout << "typed_replacements=" << replacements << '\n';
    for (const auto &[kind, count] : shapeKinds) {
      std::cout << "shape_" << kind << '=' << count << '\n';
    }
  } catch (const std::exception &error) {
    std::cerr << "delphi_geometry_audit: " << error.what() << '\n';
    return 1;
  }
}
