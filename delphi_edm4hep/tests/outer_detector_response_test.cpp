#include "delphi_edm4hep/Simulation/OuterDetectorDriftResponse.h"
#include "delphi_edm4hep/Simulation/OuterDetectorPhysicalChannelPayload.h"
#include "delphi_edm4hep/Simulation/OuterDetectorReadoutGeometry.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
}

int main() {
  using namespace delphi_edm4hep::simulation;
  OuterDetectorDriftResponse response;
  require(std::abs(response.driftTimeNs(0.0, 0.0) -
                   response.driftTimeNs(0.035, 0.0)) < 1e-12,
          "OD avalanche-region clamp changed");
  for (const auto angle : {0.0, 0.31, 0.79, 1.2}) {
    for (unsigned int sample = 1; sample <= 20; ++sample) {
      const auto distance = 0.04 + sample * 0.05;
      const auto time = response.driftTimeNs(distance, angle);
      const auto inverse = response.distanceCm(time, angle, 1.2);
      require(inverse && std::abs(*inverse - distance) < 1e-9,
              "OD SODSTM inversion changed");
    }
  }
  const OuterDetectorPhysicalChannelPayload payload{123.4567, -42.1234,
                                                     -1.2345674, 21.9876};
  const auto decoded = decodeOuterDetectorPhysicalChannelPayload(
      encodeOuterDetectorPhysicalChannelPayload(payload));
  require(std::abs(decoded.driftTimeNs - payload.driftTimeNs) <= 0.0005 &&
              std::abs(decoded.zCm - payload.zCm) <= 0.0005 &&
              std::abs(decoded.driftAngleRadians - payload.driftAngleRadians) <=
                  0.0000005 &&
              std::abs(decoded.pulseWidthNs - payload.pulseWidthNs) <= 0.0005,
          "OD physical-channel payload round trip changed");

  const OuterDetectorAddress address{24, 5, 29,
                                     OuterDetectorDriftSide::Positive};
  const auto decodedAddress = OuterDetectorReadoutGeometry::decodeCellID(
      OuterDetectorReadoutGeometry::encodeCellID(address));
  require(decodedAddress.plank == address.plank &&
              decodedAddress.layer == address.layer &&
              decodedAddress.column == address.column &&
              decodedAddress.side == address.side,
          "OD address round trip changed");
}
