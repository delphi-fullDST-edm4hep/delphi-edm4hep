#include "delphi_edm4hep/Simulation/VertexChannelResponse.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace delphi_edm4hep::simulation {

VertexChannelSample VertexChannelResponse::digitize(
    double depositedEnergyGeV, VertexBarrelLayer layerValue,
    VertexReadoutSide side, double gaussianNoiseDeviate) const {
  if (!std::isfinite(depositedEnergyGeV) || depositedEnergyGeV < 0 ||
      !std::isfinite(gaussianNoiseDeviate)) {
    throw std::invalid_argument("invalid DELPHI vertex channel response input");
  }
  const auto &layer = conditions_.layer(layerValue);
  const auto noise = side == VertexReadoutSide::P ? layer.pNoiseElectrons
                                                  : layer.nNoiseElectrons;
  const auto threshold = side == VertexReadoutSide::P ? layer.pThresholdSigma
                                                      : layer.nThresholdSigma;
  const auto depositedElectrons =
      depositedEnergyGeV * 1.0e9 / conditions_.electronHoleEnergyEv();
  const auto signalElectrons =
      std::max(0.0, depositedElectrons + noise * gaussianNoiseDeviate);
  const auto signalAdc = signalElectrons / conditions_.electronsPerAdc();
  const auto noiseAdc = noise / conditions_.electronsPerAdc();
  constexpr auto electronChargeFc = 1.602176634e-4;
  return {
      signalElectrons,
      noise,
      signalAdc,
      noiseAdc,
      signalElectrons * electronChargeFc,
      static_cast<std::int32_t>(std::lround(signalAdc * 4.0)) & 0x1fff,
      std::min<std::int32_t>(
          255, static_cast<std::int32_t>(std::lround(noiseAdc * 4.0))),
      signalElectrons > threshold * noise,
  };
}

} // namespace delphi_edm4hep::simulation
