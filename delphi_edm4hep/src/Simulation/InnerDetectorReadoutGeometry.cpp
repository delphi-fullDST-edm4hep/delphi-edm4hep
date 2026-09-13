#include "delphi_edm4hep/Simulation/InnerDetectorReadoutGeometry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace delphi_edm4hep::simulation {
namespace {

const geometry::CargoRecord &
recordOfKind(const geometry::CargoDatabase &database, std::string_view kind,
             std::string_view path) {
  const geometry::CargoRecord *found{};
  for (const auto &candidate : database.records()) {
    if (candidate.kind == kind && candidate.path == path) {
      if (found != nullptr) {
        throw std::runtime_error("duplicate ID " + std::string(kind) +
                                 " record: " + std::string(path));
      }
      found = &candidate;
    }
  }
  if (found == nullptr) {
    throw std::runtime_error("missing ID " + std::string(kind) +
                             " record: " + std::string(path));
  }
  return *found;
}

const geometry::CargoRecord &record(const geometry::CargoDatabase &database,
                                    std::string_view path) {
  return recordOfKind(database, "CALB", path);
}

double firstSectorMidPhi(const geometry::CargoDatabase &database) {
  const auto &source = recordOfKind(database, "GEOM", "/ID**/JET*/0001.B");
  const auto *field = source.findField("SHAP");
  if (field == nullptr) {
    throw std::runtime_error("missing ID first-sector shape");
  }
  auto text = field->value;
  for (const auto &line : field->continuation) {
    text += ' ';
    text += line;
  }
  std::replace(text.begin(), text.end(), ',', ' ');
  std::istringstream input(text);
  std::size_t count{};
  std::string shape;
  double phi1Degrees{};
  double phi2Degrees{};
  if (!(input >> count >> shape >> phi1Degrees >> phi2Degrees) || count != 7 ||
      shape != "CYL1") {
    throw std::runtime_error("invalid ID first-sector shape");
  }
  auto widthDegrees = phi2Degrees - phi1Degrees;
  if (widthDegrees < 0) {
    widthDegrees += 360.0;
  }
  return (phi2Degrees - 0.5 * widthDegrees) * std::numbers::pi / 180.0;
}

double normalizedPhi(double value) {
  value = std::fmod(value, 2.0 * std::numbers::pi);
  if (value < 0) {
    value += 2.0 * std::numbers::pi;
  }
  return value;
}

double signedPhi(double value) {
  value = normalizedPhi(value);
  if (value >= std::numbers::pi) {
    value -= 2.0 * std::numbers::pi;
  }
  return value;
}

std::vector<double> values(const geometry::CargoRecord &source,
                           std::string_view fieldName) {
  const auto *field = source.findField(fieldName);
  if (field == nullptr) {
    throw std::runtime_error("missing ID " + std::string(fieldName) +
                             " field: " + source.path);
  }
  auto text = field->value;
  for (const auto &line : field->continuation) {
    text += ' ';
    text += line;
  }
  std::replace(text.begin(), text.end(), ',', ' ');
  std::replace(text.begin(), text.end(), 'D', 'E');
  std::replace(text.begin(), text.end(), 'd', 'e');
  std::istringstream input(text);
  std::size_t count{};
  if (!(input >> count)) {
    throw std::runtime_error("invalid ID field count: " + source.path);
  }
  std::vector<double> result;
  double value{};
  while (input >> value) {
    result.push_back(value);
  }
  if (result.size() != count) {
    throw std::runtime_error("ID field count mismatch: " + source.path);
  }
  return result;
}

std::string numberedPath(std::string_view part, unsigned int number,
                         std::string_view sensor = {}) {
  std::ostringstream path;
  path << "/ID**/" << part << '/' << std::setw(4) << std::setfill('0')
       << number;
  if (!sensor.empty()) {
    path << ".SENS$" << sensor;
  }
  path << ".B";
  return path.str();
}

std::int32_t integer(double value, const std::string &context) {
  const auto rounded = std::lround(value);
  if (std::abs(value - static_cast<double>(rounded)) > 1e-8) {
    throw std::runtime_error("non-integral ID field value: " + context);
  }
  return static_cast<std::int32_t>(rounded);
}

template <std::size_t N>
void fillTriggerChannels(std::array<InnerDetectorTriggerChannel, N> &channels,
                         const std::vector<double> &calibration,
                         const std::vector<double> &status,
                         const std::string &context) {
  if (calibration.size() != 2 * N || status.size() != N) {
    throw std::runtime_error("invalid ID trigger channel payload: " + context);
  }
  for (std::size_t index = 0; index < N; ++index) {
    channels[index] = {static_cast<std::uint32_t>(index + 1),
                       {calibration[2 * index], calibration[2 * index + 1]},
                       integer(status[index], context)};
  }
}

} // namespace

InnerDetectorReadoutGeometry InnerDetectorReadoutGeometry::fromCargo(
    const geometry::CargoDatabase &database) {
  InnerDetectorReadoutGeometry result;
  result.jetFirstSectorMidPhiRadians_ = firstSectorMidPhi(database);

  const auto timeZeros =
      values(record(database, numberedPath("JET*", 1)), "SHAR");
  if (timeZeros.size() != 3) {
    throw std::runtime_error("invalid ID time-zero payload");
  }
  result.driftTimeZeroNs_ = timeZeros[0];
  result.cathodeTimeZeroNs_ = timeZeros[1];
  result.bunchTimeZeroNs_ = timeZeros[2];

  for (unsigned int sector = 1; sector <= result.jetSectors_.size(); ++sector) {
    const auto path = numberedPath("JET*", sector, "WIRE");
    const auto &source = record(database, path);
    const auto lead = values(source, "LEAD");
    const auto location = values(source, "LOCC");
    const auto size = values(source, "SIZC");
    const auto calibration = values(source, "CALW");
    const auto status = values(source, "STAT");
    constexpr std::size_t wires = 24;
    constexpr std::size_t calibrationWords = 21;
    if (lead.size() != 12 || integer(lead[0], path) != wires ||
        location.size() != 2 * wires || size.size() != 1 ||
        calibration.size() != wires * calibrationWords ||
        status.size() != wires) {
      throw std::runtime_error("invalid ID jet readout payload: " + path);
    }
    auto &destination = result.jetSectors_[sector - 1];
    destination.sector = sector;
    destination.halfLengthCm = 0.5 * size[0];
    for (std::size_t wire = 0; wire < wires; ++wire) {
      auto &channel = destination.wires[wire];
      channel.wire = wire + 1;
      channel.radiusCm = location[2 * wire];
      std::copy_n(calibration.begin() + wire * calibrationWords,
                  calibrationWords, channel.calibration.begin());
      channel.status = integer(status[wire], path);
    }
  }

  for (unsigned int layer = 1; layer <= result.triggerLayers_.size(); ++layer) {
    const auto anodePath = numberedPath("TRIG", layer, "WIRE");
    const auto cathodePath = numberedPath("TRIG", layer, "STRP");
    const auto &anode = record(database, anodePath);
    const auto &cathode = record(database, cathodePath);
    const auto anodeLead = values(anode, "LEAD");
    const auto anodeLocation = values(anode, "LOCC");
    const auto anodeSize = values(anode, "SIZC");
    const auto cathodeLead = values(cathode, "LEAD");
    const auto cathodeLocation = values(cathode, "LOCC");
    if (anodeLead.size() != 12 || cathodeLead.size() != 12 ||
        integer(anodeLead[0], anodePath) != 192 ||
        integer(cathodeLead[0], cathodePath) != 192 ||
        anodeLocation.size() != 3 || cathodeLocation.size() != 3 ||
        anodeSize.size() != 1) {
      throw std::runtime_error("invalid ID trigger geometry payload");
    }

    auto &destination = result.triggerLayers_[layer - 1];
    destination.layer = layer;
    destination.halfLengthCm = 0.5 * anodeSize[0];
    destination.anodeRadiusCm = anodeLocation[0];
    destination.anodeFirstPhiRadians =
        anodeLocation[1] * std::numbers::pi / 180.0;
    destination.anodePitchRadians = anodeLocation[2] * std::numbers::pi / 180.0;
    // SIGEOM advances odd layers by one wire pitch.
    if (layer % 2 == 1) {
      destination.anodeFirstPhiRadians += destination.anodePitchRadians;
    }
    destination.cathodeRadiusCm = cathodeLocation[0];
    destination.cathodeFirstZCm = cathodeLocation[1];
    destination.cathodeWidthCm = cathodeLocation[2];
    fillTriggerChannels(destination.anodes, values(anode, "CALW"),
                        values(anode, "STAT"), anodePath);
    fillTriggerChannels(destination.cathodes, values(cathode, "CALW"),
                        values(cathode, "STAT"), cathodePath);
  }

  return result;
}

std::optional<InnerDetectorTriggerAddress>
InnerDetectorReadoutGeometry::locateAnode(std::uint32_t layer, double xCm,
                                          double yCm) const {
  if (layer < 1 || layer > triggerLayers_.size()) {
    return std::nullopt;
  }
  const auto &geometry = triggerLayers_[layer - 1];
  auto phi = std::fmod(std::atan2(yCm, xCm) - geometry.anodeFirstPhiRadians,
                       2.0 * std::numbers::pi);
  if (phi < 0) {
    phi += 2.0 * std::numbers::pi;
  }
  auto wire = static_cast<std::uint32_t>(
                  std::lround(phi / geometry.anodePitchRadians)) +
              1;
  if (wire > geometry.anodes.size()) {
    wire -= geometry.anodes.size();
  }
  return InnerDetectorTriggerAddress{layer, InnerDetectorTriggerSide::Anode,
                                     wire};
}

std::optional<InnerDetectorTriggerAddress>
InnerDetectorReadoutGeometry::locateCathode(std::uint32_t layer,
                                            double zCm) const {
  if (layer < 1 || layer > triggerLayers_.size()) {
    return std::nullopt;
  }
  const auto &geometry = triggerLayers_[layer - 1];
  const auto strip = static_cast<std::int32_t>(zCm / geometry.cathodeWidthCm +
                                               0.5 * geometry.cathodes.size()) +
                     1;
  if (strip < 1 ||
      strip > static_cast<std::int32_t>(geometry.cathodes.size())) {
    return std::nullopt;
  }
  return InnerDetectorTriggerAddress{layer, InnerDetectorTriggerSide::Cathode,
                                     static_cast<std::uint32_t>(strip)};
}

double InnerDetectorReadoutGeometry::anodePhi(std::uint32_t layer,
                                              std::uint32_t wire) const {
  if (layer < 1 || layer > triggerLayers_.size() || wire < 1 ||
      wire > triggerLayers_[layer - 1].anodes.size()) {
    throw std::out_of_range("invalid ID trigger anode address");
  }
  const auto &geometry = triggerLayers_[layer - 1];
  return std::fmod(geometry.anodeFirstPhiRadians +
                       (wire - 1) * geometry.anodePitchRadians,
                   2.0 * std::numbers::pi);
}

double InnerDetectorReadoutGeometry::cathodeZ(std::uint32_t layer,
                                              std::uint32_t strip) const {
  if (layer < 1 || layer > triggerLayers_.size() || strip < 1 ||
      strip > triggerLayers_[layer - 1].cathodes.size()) {
    throw std::out_of_range("invalid ID trigger cathode address");
  }
  const auto &geometry = triggerLayers_[layer - 1];
  return geometry.cathodeFirstZCm + (strip - 1) * geometry.cathodeWidthCm;
}

std::optional<InnerDetectorJetAddress>
InnerDetectorReadoutGeometry::locateJet(double xCm, double yCm,
                                        double zCm) const {
  if (!std::isfinite(xCm) || !std::isfinite(yCm) || !std::isfinite(zCm)) {
    return std::nullopt;
  }
  const auto radius = std::hypot(xCm, yCm);
  const auto &referenceWires = jetSectors_.front().wires;
  const auto innerBoundary =
      referenceWires[0].radiusCm -
      0.5 * (referenceWires[1].radiusCm - referenceWires[0].radiusCm);
  const auto outerBoundary =
      referenceWires.back().radiusCm +
      0.5 * (referenceWires.back().radiusCm -
             referenceWires[referenceWires.size() - 2].radiusCm);
  if (radius < innerBoundary || radius >= outerBoundary ||
      std::abs(zCm) >= jetSectors_.front().halfLengthCm) {
    return std::nullopt;
  }

  const auto sectorWidth = 2.0 * std::numbers::pi / jetSectors_.size();
  const auto relative = normalizedPhi(
      std::atan2(yCm, xCm) - jetFirstSectorMidPhiRadians_ + 0.5 * sectorWidth);
  const auto sectorIndex = std::min(
      static_cast<std::size_t>(relative / sectorWidth), jetSectors_.size() - 1);
  const auto sector = static_cast<std::uint32_t>(sectorIndex + 1);
  const auto localPhi =
      signedPhi(std::atan2(yCm, xCm) - jetSectorMidPhi(sector));
  const auto &wires = jetSectors_[sectorIndex].wires;
  const auto nearest =
      std::min_element(wires.begin(), wires.end(),
                       [radius](const auto &left, const auto &right) {
                         return std::abs(left.radiusCm - radius) <
                                std::abs(right.radiusCm - radius);
                       });
  return InnerDetectorJetAddress{sector, nearest->wire,
                                 localPhi > 0 ? InnerDetectorDriftSide::Right
                                              : InnerDetectorDriftSide::Left,
                                 localPhi};
}

std::vector<InnerDetectorJetCrossing>
InnerDetectorReadoutGeometry::jetWireCrossings(
    const std::array<double, 3> &startCm,
    const std::array<double, 3> &endCm) const {
  std::vector<InnerDetectorJetCrossing> result;
  const auto dx = endCm[0] - startCm[0];
  const auto dy = endCm[1] - startCm[1];
  const auto dz = endCm[2] - startCm[2];
  const auto quadratic = dx * dx + dy * dy;
  if (quadratic <= 0.0) {
    return result;
  }
  const auto linear = 2.0 * (startCm[0] * dx + startCm[1] * dy);
  for (const auto &wire : jetSectors_.front().wires) {
    const auto constant = startCm[0] * startCm[0] + startCm[1] * startCm[1] -
                          wire.radiusCm * wire.radiusCm;
    const auto discriminant = linear * linear - 4.0 * quadratic * constant;
    if (discriminant < 0.0) {
      continue;
    }
    const auto root = std::sqrt(discriminant);
    const std::array<double, 2> fractions{(-linear - root) / (2.0 * quadratic),
                                          (-linear + root) / (2.0 * quadratic)};
    for (std::size_t solution = 0; solution < fractions.size(); ++solution) {
      auto fraction = fractions[solution];
      if (fraction < -1e-12 || fraction > 1.0 + 1e-12 ||
          (solution == 1 && std::abs(fractions[1] - fractions[0]) < 1e-12)) {
        continue;
      }
      fraction = std::clamp(fraction, 0.0, 1.0);
      std::array<double, 3> position{startCm[0] + fraction * dx,
                                     startCm[1] + fraction * dy,
                                     startCm[2] + fraction * dz};
      auto address = locateJet(position[0], position[1], position[2]);
      if (!address) {
        continue;
      }
      address->wire = wire.wire;
      result.push_back({*address, position, fraction});
    }
  }
  std::sort(result.begin(), result.end(),
            [](const auto &left, const auto &right) {
              return left.pathFraction < right.pathFraction;
            });
  return result;
}

double
InnerDetectorReadoutGeometry::jetSectorMidPhi(std::uint32_t sector) const {
  if (sector < 1 || sector > jetSectors_.size()) {
    throw std::out_of_range("invalid ID jet sector");
  }
  return normalizedPhi(jetFirstSectorMidPhiRadians_ + (sector - 1) * 2.0 *
                                                          std::numbers::pi /
                                                          jetSectors_.size());
}

std::uint64_t InnerDetectorReadoutGeometry::encodeJetCellID(
    const InnerDetectorJetAddress &address) {
  if (address.sector < 1 || address.sector > 24 || address.wire < 1 ||
      address.wire > 24) {
    throw std::out_of_range("invalid ID jet address");
  }
  return (std::uint64_t{2} << 56U) |
         (static_cast<std::uint64_t>(address.sector) << 48U) |
         (static_cast<std::uint64_t>(address.wire) << 40U) |
         (static_cast<std::uint64_t>(address.side ==
                                     InnerDetectorDriftSide::Right)
          << 39U);
}

std::uint64_t InnerDetectorReadoutGeometry::encodeJetChannelID(
    const InnerDetectorJetChannel &channel) {
  if (channel.sector < 1 || channel.sector > 24 || channel.wire < 1 ||
      channel.wire > 24) {
    throw std::out_of_range("invalid ID jet channel");
  }
  return (std::uint64_t{2} << 56U) |
         (static_cast<std::uint64_t>(channel.sector) << 48U) |
         (static_cast<std::uint64_t>(channel.wire) << 40U);
}

InnerDetectorJetChannel
InnerDetectorReadoutGeometry::decodeJetChannelID(std::uint64_t cellID) {
  constexpr std::uint64_t knownBits = 0xffff'ff00'0000'0000ULL;
  if ((cellID >> 56U) != 2 || (cellID & ~knownBits) != 0) {
    throw std::invalid_argument("invalid ID jet channel cell ID");
  }
  const auto sector = static_cast<std::uint32_t>((cellID >> 48U) & 0xffU);
  const auto wire = static_cast<std::uint32_t>((cellID >> 40U) & 0xffU);
  if (sector < 1 || sector > 24 || wire < 1 || wire > 24) {
    throw std::invalid_argument("invalid ID jet channel cell-ID address");
  }
  return {sector, wire};
}

InnerDetectorJetAddress
InnerDetectorReadoutGeometry::decodeJetCellID(std::uint64_t cellID) {
  constexpr std::uint64_t knownBits = 0xffff'ff80'0000'0000ULL;
  if ((cellID >> 56U) != 2 || (cellID & ~knownBits) != 0) {
    throw std::invalid_argument("invalid ID jet cell ID");
  }
  const auto sector = static_cast<std::uint32_t>((cellID >> 48U) & 0xffU);
  const auto wire = static_cast<std::uint32_t>((cellID >> 40U) & 0xffU);
  if (sector < 1 || sector > 24 || wire < 1 || wire > 24) {
    throw std::invalid_argument("invalid ID jet cell-ID address");
  }
  return {sector, wire,
          (cellID >> 39U) & 1U ? InnerDetectorDriftSide::Right
                               : InnerDetectorDriftSide::Left,
          0.0};
}

} // namespace delphi_edm4hep::simulation
