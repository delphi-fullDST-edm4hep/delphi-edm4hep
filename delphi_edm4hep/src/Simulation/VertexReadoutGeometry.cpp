#include "delphi_edm4hep/Simulation/VertexReadoutGeometry.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace delphi_edm4hep::simulation {
namespace {

std::vector<std::string_view> components(std::string_view path) {
  std::vector<std::string_view> result;
  for (std::size_t begin = 0; begin < path.size();) {
    const auto slash = path.find('/', begin);
    const auto end = slash == std::string_view::npos ? path.size() : slash;
    if (end != begin) {
      result.push_back(path.substr(begin, end - begin));
    }
    if (slash == std::string_view::npos) {
      break;
    }
    begin = slash + 1;
  }
  return result;
}

std::uint32_t unsignedNumber(std::string_view value,
                             std::string_view description) {
  std::uint32_t result{};
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
    throw std::runtime_error("invalid " + std::string(description) + ": " +
                             std::string(value));
  }
  return result;
}

VertexBarrelLayer layer(std::string_view name) {
  if (name == "CLLA") {
    return VertexBarrelLayer::Closer;
  }
  if (name == "INLA") {
    return VertexBarrelLayer::Inner;
  }
  if (name == "OULA") {
    return VertexBarrelLayer::Outer;
  }
  throw std::runtime_error("unknown DELPHI vertex barrel layer: " +
                           std::string(name));
}

const geometry::CargoRecord &record(const geometry::CargoDatabase &database,
                                    std::string_view path) {
  const geometry::CargoRecord *found{};
  for (const auto &candidate : database.records()) {
    if (candidate.kind == "GEOM" && candidate.path == path) {
      if (found != nullptr) {
        throw std::runtime_error("duplicate DELPHI vertex geometry record: " +
                                 std::string(path));
      }
      found = &candidate;
    }
  }
  if (found == nullptr) {
    throw std::runtime_error("missing DELPHI vertex geometry record: " +
                             std::string(path));
  }
  return *found;
}

std::vector<double> dbfValues(const geometry::CargoRecord &source,
                              std::string_view name) {
  for (const auto &field : source.fields) {
    if (field.name != "DBF") {
      continue;
    }
    const auto separator = field.value.find_first_of(" \t(");
    if (std::string_view(field.value).substr(0, separator) != name) {
      continue;
    }
    std::string text;
    for (const auto &line : field.continuation) {
      text += ' ';
      text += line;
    }
    std::replace(text.begin(), text.end(), ',', ' ');
    std::replace(text.begin(), text.end(), 'D', 'E');
    std::replace(text.begin(), text.end(), 'd', 'e');
    std::istringstream input(text);
    std::size_t count{};
    if (!(input >> count)) {
      throw std::runtime_error("invalid DBF " + std::string(name) +
                               " count at " + source.path);
    }
    std::vector<double> values;
    double value{};
    while (input >> value) {
      values.push_back(value);
    }
    if (values.size() != count) {
      throw std::runtime_error("DBF " + std::string(name) +
                               " count mismatch at " + source.path);
    }
    return values;
  }
  throw std::runtime_error("missing DBF " + std::string(name) + " at " +
                           source.path);
}

VertexActiveLine activeLine(const geometry::CargoDatabase &database,
                            std::string_view path) {
  const auto values = dbfValues(record(database, path), "USER");
  if (values.size() != 6) {
    throw std::runtime_error("DBF USER must contain two points at " +
                             std::string(path));
  }
  return {{values[0], values[1], values[2]}, {values[3], values[4], values[5]}};
}

VertexRigidTransform transform(const geometry::GeometryModel &geometry,
                               const geometry::GeometryNode &node) {
  const auto *definition = geometry.shapeDefinition(node);
  const auto &references =
      node.references.empty() ? definition->references : node.references;
  if (references.size() != 1 || !references.front().hasRotationMatrix) {
    throw std::runtime_error("vertex sensor requires one DBF MTRX: " +
                             node.path);
  }
  const auto &reference = references.front();
  VertexRigidTransform result{reference.translationCm,
                              reference.rotationMatrix};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t other = 0; other < 3; ++other) {
      double dot{};
      for (std::size_t column = 0; column < 3; ++column) {
        dot += result.rotation[row * 3 + column] *
               result.rotation[other * 3 + column];
      }
      const auto expected = row == other ? 1.0 : 0.0;
      if (std::abs(dot - expected) > 1.0e-4) {
        throw std::runtime_error("non-orthonormal vertex MTRX at " + node.path);
      }
    }
  }
  return result;
}

bool isSiliconSensor(const geometry::GeometryModel &geometry,
                     const geometry::GeometryNode &node) {
  if (!node.path.starts_with("/VD**/")) {
    return false;
  }
  const auto *definition = geometry.shapeDefinition(node);
  const auto &materials =
      node.materials.empty() ? definition->materials : node.materials;
  return !materials.empty() && materials.front().inner == "SI**";
}

std::string nSidePath(std::string path) {
  const auto plaquette = path.rfind("/PLP");
  if (plaquette == std::string::npos) {
    throw std::runtime_error("invalid vertex sensor path: " + path);
  }
  path.replace(plaquette + 1, 3, "PLN");
  return path;
}

} // namespace

std::array<double, 3> VertexRigidTransform::localToGlobal(
    const std::array<double, 3> &localCm) const {
  std::array<double, 3> result = translationCm;
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      result[row] += rotation[row * 3 + column] * localCm[column];
    }
  }
  return result;
}

std::array<double, 3> VertexRigidTransform::globalToLocal(
    const std::array<double, 3> &globalCm) const {
  std::array<double, 3> result{};
  for (std::size_t column = 0; column < 3; ++column) {
    for (std::size_t row = 0; row < 3; ++row) {
      result[column] +=
          rotation[row * 3 + column] * (globalCm[row] - translationCm[row]);
    }
  }
  return result;
}

double VertexActiveLine::lengthCm() const {
  double squared{};
  for (std::size_t index = 0; index < 3; ++index) {
    const auto delta = secondCm[index] - firstCm[index];
    squared += delta * delta;
  }
  return std::sqrt(squared);
}

VertexReadoutGeometry::VertexReadoutGeometry(std::vector<VertexSensor> sensors)
    : sensors_(std::move(sensors)) {
  if (sensors_.empty()) {
    throw std::runtime_error("DELPHI vertex readout geometry is empty");
  }
}

VertexReadoutGeometry VertexReadoutGeometry::fromCargo(
    const geometry::CargoDatabase &database,
    const geometry::GeometryModel &geometry,
    const VertexDigitizationConditions &conditions) {
  std::vector<VertexSensor> sensors;
  for (const auto &node : geometry.nodes()) {
    if (!isSiliconSensor(geometry, node)) {
      continue;
    }
    const auto path = components(node.path);
    if (path.size() != 6 || path[0] != "VD**" ||
        !(path[1] == "HSL*" || path[1] == "HSR*") ||
        !path[3].starts_with("MD") || path[3].size() != 4 ||
        !(path[4] == "HMDA" || path[4] == "HMDC") ||
        !path[5].starts_with("PLP") || path[5].size() != 6 ||
        !path[5].ends_with(".B")) {
      throw std::runtime_error("unexpected DELPHI vertex sensor path: " +
                               node.path);
    }
    const auto layerValue = layer(path[2]);
    const auto module = unsignedNumber(path[3].substr(2), "vertex module");
    const auto physicalPlaquette =
        unsignedNumber(path[5].substr(3, 1), "vertex plaquette");
    const auto *definition = geometry.shapeDefinition(node);
    if (definition->shapes.size() != 1 ||
        definition->shapes.front().kind != geometry::DelphiShapeKind::Brick ||
        definition->shapes.front().parameters.size() != 3) {
      throw std::runtime_error("vertex sensor is not one BRIK: " + node.path);
    }
    const auto readout =
        conditions.plaquette(layerValue, module, physicalPlaquette);
    const auto ordinal = static_cast<std::uint32_t>(sensors.size() + 1);
    VertexSensor sensor;
    sensor.semanticSensor = ordinal;
    sensor.cellIDBase = cellIDBase(ordinal);
    sensor.path = node.path;
    sensor.layer = layerValue;
    sensor.module = module;
    sensor.physicalPlaquette = physicalPlaquette;
    sensor.halfModuleSign = path[4] == "HMDA" ? -1 : 1;
    sensor.longitudinalRegion = readout.longitudinalRegion;
    std::copy_n(definition->shapes.front().parameters.begin(), 3,
                sensor.dimensionsCm.begin());
    sensor.readout = readout;
    sensor.pTransform = transform(geometry, node);
    sensor.pActiveLine = activeLine(database, node.path);
    if (readout.nReadoutEnabled()) {
      const auto nPath = nSidePath(node.path);
      const auto *nNode = geometry.findNode(nPath);
      if (nNode == nullptr) {
        throw std::runtime_error("missing vertex N-side plane: " + nPath);
      }
      sensor.nTransform = transform(geometry, *nNode);
      sensor.nActiveLine = activeLine(database, nPath);
    }
    sensors.push_back(std::move(sensor));
  }
  return VertexReadoutGeometry(std::move(sensors));
}

const VertexSensor &
VertexReadoutGeometry::sensor(std::uint32_t semanticSensorValue) const {
  if (semanticSensorValue == 0 || semanticSensorValue > sensors_.size()) {
    throw std::out_of_range("unknown DELPHI vertex semantic sensor");
  }
  return sensors_[semanticSensorValue - 1];
}

const VertexSensor &
VertexReadoutGeometry::sensorForTransportCellID(std::uint64_t cellID) const {
  return sensor(semanticSensor(cellID));
}

VertexElectronicsAddress
VertexReadoutGeometry::electronicsAddress(const VertexSensor &sensorValue,
                                          VertexReadoutSide side,
                                          std::uint32_t readoutStrip) const {
  const auto &canonical = sensor(sensorValue.semanticSensor);
  if (&canonical != &sensorValue) {
    throw std::invalid_argument(
        "vertex sensor does not belong to this geometry");
  }
  const auto maximumStrip = side == VertexReadoutSide::P
                                ? canonical.readout.pReadoutChannels
                                : canonical.readout.nReadoutChannels;
  if (readoutStrip == 0 || readoutStrip > maximumStrip) {
    throw std::out_of_range("vertex readout strip is outside the sensor");
  }

  const auto nzi = canonical.physicalPlaquette <= 2 ? 1U : 2U;
  const auto central =
      canonical.longitudinalRegion == VertexLongitudinalRegion::Central;
  const auto module = canonical.module;
  // This is Fortran INT(NMI/2.-0.25) from SVELCH, which truncates toward zero.
  const auto pair = static_cast<std::uint32_t>(
      static_cast<int>(static_cast<double>(module) / 2.0 - 0.25));
  VertexElectronicsAddress address;

  if (side == VertexReadoutSide::P) {
    switch (canonical.layer) {
    case VertexBarrelLayer::Outer:
      address.sirocco = (nzi == 1 ? 47U : 48U) + 2U * module;
      if (nzi == 1) {
        address.channel = central ? 1281U - readoutStrip : readoutStrip;
      } else {
        address.channel = central ? 640U + readoutStrip : 641U - readoutStrip;
      }
      break;
    case VertexBarrelLayer::Inner:
      address.sirocco = (nzi == 1 ? 25U : 26U) + 2U * pair;
      if (nzi == 1) {
        address.channel = module % 2 == 0 ? 640U + readoutStrip : readoutStrip;
      } else {
        address.channel =
            module % 2 == 0 ? 1281U - readoutStrip : 513U - readoutStrip;
      }
      break;
    case VertexBarrelLayer::Closer:
      address.sirocco = (nzi == 1 ? 1U : 2U) + 2U * pair;
      if (nzi == 1) {
        if (central) {
          address.channel =
              module % 2 == 0 ? 1152U + readoutStrip : 384U + readoutStrip;
        } else {
          address.channel =
              module % 2 == 0 ? 1153U - readoutStrip : 385U - readoutStrip;
        }
      } else if (central) {
        address.channel =
            module % 2 == 0 ? 769U - readoutStrip : 1537U - readoutStrip;
      } else {
        address.channel = module % 2 == 0 ? readoutStrip : 768U + readoutStrip;
      }
      break;
    }
  } else {
    switch (canonical.layer) {
    case VertexBarrelLayer::Inner:
      throw std::invalid_argument("v94c inner VD has no N-side readout");
    case VertexBarrelLayer::Outer:
      address.sirocco = (nzi == 1 ? 47U : 48U) + 2U * module;
      address.channel =
          central ? (1280U - readoutStrip) % 640U + 1U : 1281U - readoutStrip;
      break;
    case VertexBarrelLayer::Closer: {
      address.sirocco = (nzi == 1 ? 1U : 2U) + 2U * pair;
      const auto firstHalf = nzi - module % 2U == 1U;
      if (central) {
        address.channel =
            (firstHalf ? 768U : 0U) + (1152U - readoutStrip) % 384U + 1U;
      } else {
        address.channel = (firstHalf ? 1537U : 769U) - readoutStrip;
      }
      break;
    }
    }
  }
  if (address.sirocco < 1 || address.sirocco > 96 || address.channel < 1 ||
      address.channel > 1536) {
    throw std::runtime_error("v94c SVELCH produced an invalid address");
  }
  return address;
}

std::uint64_t
VertexReadoutGeometry::cellIDBase(std::uint32_t semanticSensorValue) {
  if (semanticSensorValue == 0 || semanticSensorValue > 0x00ffffffU) {
    throw std::out_of_range("vertex semantic sensor exceeds cell-ID field");
  }
  return (static_cast<std::uint64_t>(subsystem) << 56U) |
         (static_cast<std::uint64_t>(semanticSensorValue) << 32U);
}

std::uint32_t
VertexReadoutGeometry::semanticSensor(std::uint64_t transportCellID) {
  if (static_cast<std::uint8_t>(transportCellID >> 56U) != subsystem) {
    throw std::invalid_argument("transport cell ID is not a DELPHI VD hit");
  }
  const auto result =
      static_cast<std::uint32_t>((transportCellID >> 32U) & 0x00ffffffU);
  if (result == 0) {
    throw std::invalid_argument("transport cell ID has no VD sensor ordinal");
  }
  return result;
}

} // namespace delphi_edm4hep::simulation
