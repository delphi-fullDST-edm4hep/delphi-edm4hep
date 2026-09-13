#include "delphi_edm4hep/Simulation/TpcWireGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace delphi_edm4hep::simulation {
namespace {

constexpr std::string_view kWirePath = "/TPC*/ENP0/SC00.SENS$WIRE.B";
constexpr double kPadWireOffsetCm = 3.3; // FILPAD in STINI

const geometry::CargoRecord &
wireRecord(const geometry::CargoDatabase &database) {
  const geometry::CargoRecord *found{};
  for (const auto &candidate : database.records()) {
    if (candidate.kind == "CALB" && candidate.path == kWirePath) {
      if (found != nullptr) {
        throw std::runtime_error("duplicate TPC wire calibration record");
      }
      found = &candidate;
    }
  }
  if (found == nullptr) {
    throw std::runtime_error("missing TPC wire calibration record");
  }
  return *found;
}

std::vector<double> values(const geometry::CargoRecord &source,
                           std::string_view fieldName) {
  const auto *field = source.findField(fieldName);
  if (field == nullptr) {
    throw std::runtime_error("missing TPC wire " + std::string(fieldName) +
                             " field");
  }
  auto payload = field->value;
  for (const auto &line : field->continuation) {
    payload += ' ';
    payload += line;
  }
  std::replace(payload.begin(), payload.end(), ',', ' ');
  std::replace(payload.begin(), payload.end(), 'D', 'E');
  std::replace(payload.begin(), payload.end(), 'd', 'e');
  std::istringstream input(payload);
  std::size_t count{};
  if (!(input >> count)) {
    throw std::runtime_error("invalid TPC wire field count");
  }
  std::vector<double> result;
  double value{};
  while (input >> value) {
    result.push_back(value);
  }
  if (result.size() != count) {
    throw std::runtime_error("TPC wire field count mismatch");
  }
  return result;
}

} // namespace

TpcWireGeometry::TpcWireGeometry(TpcReadoutGeometry readout,
                                 unsigned int wireCount, double wireSpacingCm,
                                 double firstWireRadiusCm,
                                 unsigned int highWireStart,
                                 double highWireHalfWidthCm,
                                 double highWireHalfWidthSlopeCm)
    : readout_(std::move(readout)), wireCount_(wireCount),
      wireSpacingCm_(wireSpacingCm), firstWireRadiusCm_(firstWireRadiusCm),
      wireReferenceCm_(firstWireRadiusCm - 0.5 * wireSpacingCm),
      highWireStart_(highWireStart), highWireHalfWidthCm_(highWireHalfWidthCm),
      highWireHalfWidthSlopeCm_(highWireHalfWidthSlopeCm) {
  if (wireCount_ == 0 || wireSpacingCm_ <= 0 || firstWireRadiusCm_ <= 0 ||
      highWireStart_ < 1 || highWireStart_ > wireCount_ ||
      highWireHalfWidthCm_ <= 0) {
    throw std::runtime_error("invalid TPC wire geometry");
  }
  if (maximumAbsLocalXCm(wireCount_) <= 0) {
    throw std::runtime_error("TPC high-wire taper closes before the last wire");
  }
}

TpcWireGeometry
TpcWireGeometry::fromCargo(const geometry::CargoDatabase &database,
                           const TpcReadoutGeometry &readout) {
  const auto &record = wireRecord(database);
  const auto lead = values(record, "LEAD");
  const auto location = values(record, "LOCC");
  const auto size = values(record, "SIZC");
  if (lead.size() != 12 || location.size() != 3 || size.size() != 5) {
    throw std::runtime_error("invalid TPC WIRE calibration payload");
  }
  const auto wireCount = static_cast<unsigned int>(std::lround(lead[0]));
  const auto highWireStart = static_cast<unsigned int>(std::lround(size[2]));
  return {readout,       wireCount,
          location[1],   location[0] - kPadWireOffsetCm,
          highWireStart, size[3] / 2.0,
          size[4] / 2.0};
}

double TpcWireGeometry::wireRadiusCm(unsigned int wire) const {
  if (wire < 1 || wire > wireCount_) {
    throw std::runtime_error("TPC wire number is out of range");
  }
  return firstWireRadiusCm_ + (wire - 1) * wireSpacingCm_;
}

double TpcWireGeometry::maximumAbsLocalXCm(unsigned int wire) const {
  if (wire < 1 || wire > wireCount_) {
    throw std::runtime_error("TPC wire number is out of range");
  }
  if (wire < highWireStart_) {
    return std::numeric_limits<double>::infinity();
  }
  // STINI sets IWMAX=WISIZ(3)-1, and STINTR evaluates
  // XWMAX+DXW*(JWIRE-IWMAX).
  return highWireHalfWidthCm_ +
         highWireHalfWidthSlopeCm_ * (wire - (highWireStart_ - 1));
}

std::optional<TpcWireAddress> TpcWireGeometry::locate(double xCm, double yCm,
                                                      double zCm,
                                                      double momentumX,
                                                      double momentumY) const {
  const auto endcap = zCm < 0 ? 0U : 1U;
  const TpcSectorTransform *bestSector{};
  double bestPhi = std::numbers::pi;
  double bestLocalX{};
  double bestLocalY{};
  double bestLocalMomentumY{};
  for (const auto &sector : readout_.sectors()) {
    if (sector.endcap != endcap) {
      continue;
    }
    const auto angle = sector.rotationDegrees * std::numbers::pi / 180.0;
    const auto dx = xCm - sector.translationXCm;
    const auto dy = yCm - sector.translationYCm;
    const auto localX = std::cos(angle) * dx + std::sin(angle) * dy;
    const auto localY = -std::sin(angle) * dx + std::cos(angle) * dy;
    const auto phi = std::atan2(localX, localY);
    if (localY > 0 && std::abs(phi) <= std::abs(bestPhi)) {
      bestSector = &sector;
      bestPhi = phi;
      bestLocalX = localX;
      bestLocalY = localY;
      bestLocalMomentumY =
          -std::sin(angle) * momentumX + std::cos(angle) * momentumY;
    }
  }
  if (bestSector == nullptr || std::abs(bestPhi) > std::numbers::pi / 6.0) {
    return std::nullopt;
  }
  const auto rawWire =
      static_cast<int>((bestLocalY - wireReferenceCm_) / wireSpacingCm_ + 1.0);
  if (rawWire < 1 || rawWire > static_cast<int>(wireCount_)) {
    return std::nullopt;
  }
  const auto wire = static_cast<unsigned int>(rawWire);
  if (std::abs(bestLocalX) > maximumAbsLocalXCm(wire)) {
    return std::nullopt;
  }
  return TpcWireAddress{endcap,     bestSector->readoutSector,
                        wire,       bestLocalMomentumY < 0 ? -1 : 1,
                        bestLocalX, std::abs(zCm)};
}

std::array<double, 3>
TpcWireGeometry::wirePoint(const TpcWireAddress &address) const {
  if (address.wire < 1 || address.wire > wireCount_ ||
      address.outwardDirection == 0) {
    throw std::runtime_error("invalid TPC wire address");
  }
  const auto sector =
      std::find_if(readout_.sectors().begin(), readout_.sectors().end(),
                   [&](const auto &entry) {
                     return entry.readoutSector == address.sector &&
                            entry.endcap == address.endcap;
                   });
  if (sector == readout_.sectors().end()) {
    throw std::runtime_error("TPC wire sector is absent from readout geometry");
  }
  const auto angle = sector->rotationDegrees * std::numbers::pi / 180.0;
  const auto localY = wireRadiusCm(address.wire);
  return {sector->translationXCm + std::cos(angle) * address.localXCm -
              std::sin(angle) * localY,
          sector->translationYCm + std::sin(angle) * address.localXCm +
              std::cos(angle) * localY,
          address.endcap == 0 ? -address.localZCm : address.localZCm};
}

} // namespace delphi_edm4hep::simulation
