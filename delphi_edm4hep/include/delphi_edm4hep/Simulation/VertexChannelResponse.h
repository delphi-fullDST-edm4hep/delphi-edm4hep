#pragma once

#include "delphi_edm4hep/Simulation/VertexDigitizationConditions.h"

#include <cstdint>

namespace delphi_edm4hep::simulation {

struct VertexChannelSample {
  double signalElectrons{};
  double noiseElectrons{};
  double signalAdc{};
  double noiseAdc{};
  double chargeFc{};
  std::int32_t packedSignalQuarterAdc{};
  std::int32_t packedNoiseQuarterAdc{};
  bool aboveSingleChannelThreshold{};
};

class VertexChannelResponse {
public:
  explicit VertexChannelResponse(VertexDigitizationConditions conditions =
                                     VertexDigitizationConditions::legacyV94c())
      : conditions_(conditions) {}

  VertexChannelSample digitize(double depositedEnergyGeV,
                               VertexBarrelLayer layer, VertexReadoutSide side,
                               double gaussianNoiseDeviate) const;

private:
  VertexDigitizationConditions conditions_;
};

} // namespace delphi_edm4hep::simulation
