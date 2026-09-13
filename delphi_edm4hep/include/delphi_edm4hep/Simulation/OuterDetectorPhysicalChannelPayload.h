#pragma once

#include <array>

namespace delphi_edm4hep::simulation {

// Transitional native payload for one calibrated physical OD tube. This is
// deliberately distinct from the legacy multiplexed crate/TDC word format.
// All four words are non-negative so they survive RawTimeSeries serialization.
struct OuterDetectorPhysicalChannelPayload {
  double driftTimeNs{};
  double zCm{};
  double driftAngleRadians{};
  double pulseWidthNs{};
};

std::array<int, 4>
encodeOuterDetectorPhysicalChannelPayload(
    const OuterDetectorPhysicalChannelPayload &payload);
OuterDetectorPhysicalChannelPayload
decodeOuterDetectorPhysicalChannelPayload(const std::array<int, 4> &words);

constexpr float outerDetectorPayloadIntervalNs = 0.001F;
constexpr int outerDetectorPhysicalChannelPayloadVersion = 1;

} // namespace delphi_edm4hep::simulation
