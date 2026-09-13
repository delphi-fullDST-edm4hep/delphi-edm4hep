#include "delphi_edm4hep/Simulation/OuterDetectorPhysicalChannelPayload.h"

#include <cmath>
#include <stdexcept>

namespace delphi_edm4hep::simulation {
namespace {
constexpr double timeUnitNs = 0.001;
constexpr double zUnitCm = 0.001;
constexpr double angleUnitRadians = 0.000001;
constexpr int zOffset = 300000;
constexpr int angleOffset = 4000000;
}

std::array<int, 4> encodeOuterDetectorPhysicalChannelPayload(
    const OuterDetectorPhysicalChannelPayload &payload) {
  if (!std::isfinite(payload.driftTimeNs) || payload.driftTimeNs < 0 ||
      !std::isfinite(payload.zCm) || std::abs(payload.zCm) >= 300.0 ||
      !std::isfinite(payload.driftAngleRadians) ||
      std::abs(payload.driftAngleRadians) >= 4.0 ||
      !std::isfinite(payload.pulseWidthNs) || payload.pulseWidthNs < 0) {
    throw std::invalid_argument("invalid OD physical-channel payload");
  }
  return {static_cast<int>(std::lround(payload.driftTimeNs / timeUnitNs)),
          static_cast<int>(std::lround(payload.zCm / zUnitCm)) + zOffset,
          static_cast<int>(std::lround(payload.driftAngleRadians /
                                       angleUnitRadians)) +
              angleOffset,
          static_cast<int>(std::lround(payload.pulseWidthNs / timeUnitNs))};
}

OuterDetectorPhysicalChannelPayload
decodeOuterDetectorPhysicalChannelPayload(const std::array<int, 4> &words) {
  if (words[0] < 0 || words[1] < 0 || words[2] < 0 || words[3] < 0) {
    throw std::invalid_argument("invalid OD physical-channel payload words");
  }
  return {words[0] * timeUnitNs, (words[1] - zOffset) * zUnitCm,
          (words[2] - angleOffset) * angleUnitRadians,
          words[3] * timeUnitNs};
}

} // namespace delphi_edm4hep::simulation
