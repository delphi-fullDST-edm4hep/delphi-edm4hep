#include "delphi_edm4hep/Simulation/OuterDetectorReadoutGeometry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace delphi_edm4hep::simulation {
namespace {

const geometry::CargoRecord &record(const geometry::CargoDatabase &database,
                                    std::string_view kind,
                                    std::string_view path) {
  const geometry::CargoRecord *found{};
  for (const auto &candidate : database.records()) {
    if (candidate.kind == kind && candidate.path == path) {
      if (found != nullptr) {
        throw std::runtime_error("duplicate OD record: " + std::string(path));
      }
      found = &candidate;
    }
  }
  if (found == nullptr) {
    throw std::runtime_error("missing OD record: " + std::string(path));
  }
  return *found;
}

std::vector<double> values(const geometry::CargoRecord &source,
                           std::string_view fieldName) {
  const auto *field = source.findField(fieldName);
  if (field == nullptr) {
    throw std::runtime_error("missing OD " + std::string(fieldName) +
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
    throw std::runtime_error("invalid OD field count: " + source.path);
  }
  std::vector<double> result;
  double value{};
  while (input >> value) {
    result.push_back(value);
  }
  if (result.size() != count) {
    throw std::runtime_error("OD field count mismatch: " + source.path);
  }
  return result;
}

std::string calibrationPath(unsigned int plank) {
  std::ostringstream path;
  path << "/OD**/";
  if (plank < 10) {
    path << "PLN" << plank;
  } else {
    path << "PL" << plank;
  }
  path << ".SENS$ODWR.B";
  return path.str();
}

constexpr std::array<unsigned int, 5> firstColumn{2, 3, 2, 1, 0};
constexpr std::array<unsigned int, 5> lastColumn{30, 31, 29, 29, 29};
constexpr std::array<unsigned int, 5> wiresPerLayer{29, 29, 28, 29, 30};
constexpr std::array<double, 5> xCorrection{0.0, -1.745 / 2.0,
                                            1.745 / 2.0, 3.0 * 1.745 / 2.0,
                                            3.0 * 1.745 / 2.0};

void validateChannel(const OuterDetectorChannel &channel) {
  if (channel.plank < 1 || channel.plank > 24 || channel.layer < 1 ||
      channel.layer > 5 ||
      channel.column < firstColumn[channel.layer - 1] ||
      channel.column > lastColumn[channel.layer - 1]) {
    throw std::out_of_range("invalid OD physical channel");
  }
}

} // namespace

OuterDetectorReadoutGeometry OuterDetectorReadoutGeometry::fromCargo(
    const geometry::CargoDatabase &database) {
  OuterDetectorReadoutGeometry result;
  const auto geometryValues =
      values(record(database, "GEOM", "/OD**.B"), "FIDS");
  if (geometryValues.size() != 436) {
    throw std::runtime_error("invalid OD FIDS survey payload");
  }
  result.cellPitchXCm_ = geometryValues[0];
  result.layerPitchYCm_ = geometryValues[1];
  if (result.cellPitchXCm_ <= 0 || result.layerPitchYCm_ <= 0) {
    throw std::runtime_error("invalid OD cell pitch");
  }

  const auto global = values(record(database, "CALB", "/OD**.B"), "USER");
  if (global.size() < 28) {
    throw std::runtime_error("invalid OD global calibration payload");
  }
  const auto globalTimeZeroNs = global[20];
  constexpr double activeZCm = 227.25;
  constexpr double surveyAngle = std::atan(5.25 / 53.413);
  const auto survey = [&geometryValues](unsigned int coordinate,
                                        unsigned int point,
                                        unsigned int end,
                                        unsigned int plank) {
    return geometryValues[4 + coordinate + 3 * point + 9 * end + 18 * plank];
  };

  result.tubes_.reserve(3480);
  for (unsigned int plankIndex = 0; plankIndex < 24; ++plankIndex) {
    const auto xSurvey1 = survey(0, 2, 0, plankIndex);
    const auto ySurvey1 = survey(1, 2, 0, plankIndex);
    const auto xSurvey2 = survey(0, 0, 0, plankIndex);
    const auto ySurvey2 = survey(1, 0, 0, plankIndex);
    auto theta = std::atan2(ySurvey2 - ySurvey1, xSurvey2 - xSurvey1) -
                 surveyAngle - std::numbers::pi / 2.0;
    if (theta < 0) {
      theta += 2.0 * std::numbers::pi;
    }
    result.plankAngles_[plankIndex] = theta;
    const auto rotation = theta - std::numbers::pi / 2.0;
    const auto cosine = std::cos(rotation);
    const auto sine = std::sin(rotation);
    const auto calibration = values(
        record(database, "CALB", calibrationPath(plankIndex + 1)), "CALW");
    if (calibration.size() != 1120) {
      throw std::runtime_error("invalid OD tube calibration payload");
    }

    for (unsigned int layerIndex = 0; layerIndex < 5; ++layerIndex) {
      const auto yOffset = (static_cast<double>(layerIndex) - 4.0) *
                           result.layerPitchYCm_;
      for (unsigned int wireIndex = 0; wireIndex < wiresPerLayer[layerIndex];
           ++wireIndex) {
        const auto column = firstColumn[layerIndex] + wireIndex;
        const auto xDistance =
            (static_cast<double>(wiresPerLayer[layerIndex]) / 2.0 -
             static_cast<double>(wireIndex + 1)) *
                result.cellPitchXCm_ +
            result.cellPitchXCm_ / 2.0 + xCorrection[layerIndex];
        const auto xOffset = xDistance - (27.052 + xCorrection[1]);
        const auto wireX = xOffset * cosine - yOffset * sine + xSurvey1;
        const auto wireY = yOffset * cosine + xOffset * sine + ySurvey1;
        const auto calibrationOffset = layerIndex * 224 + column * 7;
        const auto efficiency = calibration[calibrationOffset + 4] / 100.0;
        result.tubes_.push_back(
            {{plankIndex + 1, layerIndex + 1, column},
             wireX,
             wireY,
             -activeZCm,
             activeZCm,
             calibration[calibrationOffset] + globalTimeZeroNs,
             calibration[calibrationOffset + 1] * 15.0 / 18.0,
             calibration[calibrationOffset + 3],
             std::clamp(efficiency, 0.0, 1.0),
             efficiency > 0.0});
      }
    }
  }
  if (result.tubes_.size() != 3480) {
    throw std::runtime_error("OD tube catalogue is incomplete");
  }
  return result;
}

const OuterDetectorTube &
OuterDetectorReadoutGeometry::tube(const OuterDetectorChannel &channel) const {
  validateChannel(channel);
  const auto withinPlank = [&channel] {
    std::size_t offset{};
    for (unsigned int layer = 1; layer < channel.layer; ++layer) {
      offset += wiresPerLayer[layer - 1];
    }
    return offset + channel.column - firstColumn[channel.layer - 1];
  }();
  return tubes_.at((channel.plank - 1) * 145 + withinPlank);
}

std::array<double, 2> OuterDetectorReadoutGeometry::wirePosition(
    const OuterDetectorTube &channel, double zCm) const {
  constexpr double sagCm = 0.03;
  constexpr double sagReferenceCm = 236.0;
  const auto sag = sagCm * zCm * zCm /
                       (sagReferenceCm * sagReferenceCm) -
                   sagCm;
  return {channel.wireXAtEndCm, channel.wireYAtEndCm + sag};
}

std::optional<OuterDetectorLocatedTube>
OuterDetectorReadoutGeometry::locate(double xCm, double yCm, double zCm) const {
  const OuterDetectorTube *best{};
  double bestX{};
  double bestY{};
  auto bestDistanceSquared = std::numeric_limits<double>::infinity();
  for (const auto &candidate : tubes_) {
    if (zCm < candidate.zNegativeCm || zCm > candidate.zPositiveCm) {
      continue;
    }
    const auto wire = wirePosition(candidate, zCm);
    const auto distanceSquared =
        (xCm - wire[0]) * (xCm - wire[0]) +
        (yCm - wire[1]) * (yCm - wire[1]);
    if (distanceSquared < bestDistanceSquared) {
      best = &candidate;
      bestX = wire[0];
      bestY = wire[1];
      bestDistanceSquared = distanceSquared;
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }
  const auto rotation = plankAngles_[best->channel.plank - 1] -
                        std::numbers::pi / 2.0;
  const auto cosine = std::cos(rotation);
  const auto sine = std::sin(rotation);
  const auto dx = xCm - bestX;
  const auto dy = yCm - bestY;
  const auto localX = dx * cosine + dy * sine;
  const auto localY = dy * cosine - dx * sine;
  if (std::abs(localX) > cellPitchXCm_ / 2.0 + 1e-6 ||
      std::abs(localY) > layerPitchYCm_ / 2.0 + 1e-6) {
    return std::nullopt;
  }
  return OuterDetectorLocatedTube{best, bestX, bestY, localX, localY,
                                  std::sqrt(bestDistanceSquared),
                                  std::atan2(localY, localX)};
}

std::array<double, 2> OuterDetectorReadoutGeometry::localToGlobalDirection(
    std::uint32_t plank, double localX, double localY) const {
  if (plank < 1 || plank > 24) {
    throw std::out_of_range("invalid OD plank");
  }
  const auto rotation = plankAngles_[plank - 1] - std::numbers::pi / 2.0;
  return {localX * std::cos(rotation) - localY * std::sin(rotation),
          localY * std::cos(rotation) + localX * std::sin(rotation)};
}

std::uint64_t OuterDetectorReadoutGeometry::encodeChannelID(
    const OuterDetectorChannel &channel) {
  validateChannel(channel);
  return (std::uint64_t{4} << 56U) |
         (static_cast<std::uint64_t>(channel.plank) << 48U) |
         (static_cast<std::uint64_t>(channel.layer) << 40U) |
         (static_cast<std::uint64_t>(channel.column) << 32U);
}

OuterDetectorChannel
OuterDetectorReadoutGeometry::decodeChannelID(std::uint64_t cellID) {
  if ((cellID >> 56U) != 4 || (cellID & 0x00000000ffffffffULL) != 0) {
    throw std::invalid_argument("invalid OD channel cell ID");
  }
  OuterDetectorChannel result{static_cast<std::uint32_t>((cellID >> 48U) & 0xff),
                              static_cast<std::uint32_t>((cellID >> 40U) & 0xff),
                              static_cast<std::uint32_t>((cellID >> 32U) & 0xff)};
  validateChannel(result);
  return result;
}

std::uint64_t
OuterDetectorReadoutGeometry::encodeCellID(const OuterDetectorAddress &address) {
  return encodeChannelID({address.plank, address.layer, address.column}) |
         (static_cast<std::uint64_t>(address.side) << 31U);
}

OuterDetectorAddress
OuterDetectorReadoutGeometry::decodeCellID(std::uint64_t cellID) {
  if ((cellID & 0x000000007fffffffULL) != 0) {
    throw std::invalid_argument("invalid OD reconstructed-hit cell ID");
  }
  const auto channel = decodeChannelID(cellID & ~(std::uint64_t{1} << 31U));
  return {channel.plank, channel.layer, channel.column,
          static_cast<OuterDetectorDriftSide>((cellID >> 31U) & 1U)};
}

} // namespace delphi_edm4hep::simulation
