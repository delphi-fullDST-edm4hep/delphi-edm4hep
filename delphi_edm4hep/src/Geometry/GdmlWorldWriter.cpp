#include "delphi_edm4hep/Geometry/GdmlWorldWriter.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string>

namespace delphi_edm4hep::geometry {
namespace {

std::string xmlEscape(std::string_view value) {
  std::string escaped;
  for (const auto character : value) {
    switch (character) {
    case '&':
      escaped += "&amp;";
      break;
    case '<':
      escaped += "&lt;";
      break;
    case '>':
      escaped += "&gt;";
      break;
    case '\"':
      escaped += "&quot;";
      break;
    case '\'':
      escaped += "&apos;";
      break;
    default:
      escaped.push_back(character);
      break;
    }
  }
  return escaped;
}

std::string gdmlName(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const auto character : value) {
    const auto valid = (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9') ||
                       character == '_';
    result.push_back(valid ? character : '_');
  }
  if (result.empty() || (result.front() >= '0' && result.front() <= '9')) {
    result.insert(result.begin(), '_');
  }
  return result;
}

} // namespace

void writeGdmlWorld(std::ostream &output, const GeometryModel &model,
                    std::string_view worldPath,
                    std::string_view snapshotIdentifier) {
  const auto world = std::find_if(
      model.nodes().begin(), model.nodes().end(),
      [worldPath](const auto &node) { return node.path == worldPath; });
  if (world == model.nodes().end()) {
    throw std::runtime_error("DELPHI GDML world node not found: " +
                             std::string(worldPath));
  }
  const auto shape = std::find_if(
      world->shapes.begin(), world->shapes.end(),
      [](const auto &candidate) { return candidate.field == "SHAP"; });
  if (shape == world->shapes.end() ||
      shape->kind != DelphiShapeKind::Cylinder1 ||
      shape->parameters.size() != 6) {
    throw std::runtime_error(
        "DELPHI GDML world must have one primary CYL1 boundary");
  }
  if (world->materials.empty()) {
    throw std::runtime_error("DELPHI GDML world has no material assignment");
  }
  const auto &materialName = world->materials.front().inner;
  const auto material =
      std::find_if(model.materials().begin(), model.materials().end(),
                   [&materialName](const auto &candidate) {
                     return candidate.name == materialName;
                   });
  if (material == model.materials().end()) {
    throw std::runtime_error("DELPHI GDML world material is undefined: " +
                             materialName);
  }

  const auto minimumPhi = shape->parameters[0];
  const auto maximumPhi = shape->parameters[1];
  const auto minimumRadius = shape->parameters[2];
  const auto maximumRadius = shape->parameters[3];
  const auto minimumZ = shape->parameters[4];
  const auto maximumZ = shape->parameters[5];
  if (maximumPhi <= minimumPhi || maximumRadius <= minimumRadius ||
      maximumZ <= minimumZ || std::abs(minimumZ + maximumZ) > 1.0e-9) {
    throw std::runtime_error("DELPHI GDML world CYL1 bounds are invalid");
  }
  const auto density = material->densityGramPerCm3;
  const auto atomicNumber = material->atomicNumber;
  const auto atomicMass = material->atomicWeightGramPerMole;
  if (density <= 0 || atomicNumber <= 0 || atomicMass <= 0) {
    throw std::runtime_error("DELPHI GDML world material values are invalid");
  }

  const auto volume = "delphi_" + gdmlName(world->name);
  const auto materialId = "delphi_material_" + gdmlName(materialName);
  const auto elementId = "delphi_element_" + gdmlName(materialName);
  output << std::setprecision(17)
         << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         << "<gdml xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
            "xsi:noNamespaceSchemaLocation=\"http://service-spi.web.cern.ch/"
            "service-spi/app/releases/GDML/schema/gdml.xsd\">\n"
         << "  <define/>\n"
         << "  <materials>\n"
         << "    <element name=\"" << elementId << "\" formula=\""
         << xmlEscape(materialName) << "\" Z=\"" << atomicNumber << "\">\n"
         << "      <atom unit=\"g/mole\" value=\"" << atomicMass << "\"/>\n"
         << "    </element>\n"
         << "    <material name=\"" << materialId << "\" state=\""
         << (density < 0.01 ? "gas" : "solid") << "\">\n"
         << "      <D unit=\"g/cm3\" value=\"" << density << "\"/>\n"
         << "      <fraction n=\"1\" ref=\"" << elementId << "\"/>\n"
         << "    </material>\n"
         << "  </materials>\n"
         << "  <solids>\n"
         << "    <tube name=\"" << volume << "_solid\" rmin=\"" << minimumRadius
         << "\" rmax=\"" << maximumRadius << "\" z=\"" << maximumZ - minimumZ
         << "\" startphi=\"" << minimumPhi << "\" deltaphi=\""
         << maximumPhi - minimumPhi << "\" aunit=\"deg\" lunit=\"cm\"/>\n"
         << "  </solids>\n"
         << "  <structure>\n"
         << "    <volume name=\"" << volume << "\">\n"
         << "      <materialref ref=\"" << materialId << "\"/>\n"
         << "      <solidref ref=\"" << volume << "_solid\"/>\n";
  if (!snapshotIdentifier.empty()) {
    output << "      <auxiliary auxtype=\"DELPHI_CARGO_SOURCE\" auxvalue=\""
           << xmlEscape(snapshotIdentifier) << "\"/>\n";
  }
  output << "    </volume>\n"
         << "  </structure>\n"
         << "  <setup name=\"DELPHI\" version=\"1.0\">\n"
         << "    <world ref=\"" << volume << "\"/>\n"
         << "  </setup>\n"
         << "</gdml>\n";
  if (!output) {
    throw std::runtime_error("failed while writing DELPHI GDML world");
  }
}

} // namespace delphi_edm4hep::geometry
