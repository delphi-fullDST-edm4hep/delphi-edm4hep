#include "delphi_edm4hep/Simulation/VertexDigitizationConditions.h"

#include <stdexcept>

namespace delphi_edm4hep::simulation {
namespace {

constexpr double pReadoutPitchCm = 0.0050;
constexpr double pPhysicalPitchCm = 0.0025;

VertexPlaquetteConditions
makePlaquette(std::uint32_t pChannels, std::uint32_t nChannels,
              std::uint32_t nFirstPitchChannels, double nFirstPitchCm,
              double nSecondPitchCm, double nSecondZoneOffsetCm,
              VertexLongitudinalRegion region) {
  return {pChannels,           2 * pChannels,
          pReadoutPitchCm,     pPhysicalPitchCm,
          nChannels,           nFirstPitchChannels,
          nFirstPitchCm,       nSecondPitchCm,
          nSecondZoneOffsetCm, region};
}

} // namespace

VertexDigitizationConditions VertexDigitizationConditions::legacyV94c() {
  VertexDigitizationConditions conditions;
  conditions.layers_[0] = {24, 4, 2500.0, 2500.0, 5.0, 5.0};
  conditions.layers_[1] = {24, 4, 1700.0, 0.0, 5.0, 0.0};
  conditions.layers_[2] = {24, 4, 2500.0, 2500.0, 5.0, 5.0};
  return conditions;
}

const VertexLayerConditions &
VertexDigitizationConditions::layer(VertexBarrelLayer layerValue) const {
  const auto index = static_cast<std::size_t>(layerValue);
  if (index < 1 || index > layers_.size()) {
    throw std::out_of_range("invalid DELPHI vertex barrel layer");
  }
  return layers_[index - 1];
}

VertexLongitudinalRegion VertexDigitizationConditions::longitudinalRegion(
    std::size_t physicalPlaquette) {
  if (physicalPlaquette < 1 || physicalPlaquette > 4) {
    throw std::out_of_range("invalid DELPHI vertex physical plaquette");
  }
  return physicalPlaquette == 2 || physicalPlaquette == 3
             ? VertexLongitudinalRegion::Central
             : VertexLongitudinalRegion::Peripheral;
}

VertexPlaquetteConditions
VertexDigitizationConditions::plaquette(VertexBarrelLayer layerValue,
                                        std::size_t module,
                                        std::size_t physicalPlaquette) const {
  const auto &layerConditions = layer(layerValue);
  if (module < 1 || module > layerConditions.modules || physicalPlaquette < 1 ||
      physicalPlaquette > layerConditions.physicalPlaquettesPerModule) {
    throw std::out_of_range("invalid DELPHI vertex module or plaquette");
  }
  const auto region = longitudinalRegion(physicalPlaquette);
  switch (layerValue) {
  case VertexBarrelLayer::Closer:
    if (region == VertexLongitudinalRegion::Central) {
      // VDSIM's VD94 transition uses 768 channels at 49.5 um followed by
      // 384 channels at 99 um, with a half-physical-strip boundary repair.
      return makePlaquette(384, 1152, 768, 0.00495, 0.00990, -0.0025, region);
    }
    return makePlaquette(384, 384, 384, 0.0150, 0.0, 0.0, region);
  case VertexBarrelLayer::Inner:
    // SVCALB models the shorter odd modules with 512 P channels and the even
    // modules with 640. The v94c inner layer has no N-side readout.
    return makePlaquette(module % 2 == 0 ? 640 : 512, 0, 0, 0.0, 0.0, 0.0,
                         region);
  case VertexBarrelLayer::Outer:
    if (region == VertexLongitudinalRegion::Central) {
      return makePlaquette(640, 1280, 1280, 0.00420, 0.0, 0.0, region);
    }
    return makePlaquette(640, 640, 640, 0.00840, 0.0, 0.0, region);
  }
  throw std::out_of_range("invalid DELPHI vertex barrel layer");
}

} // namespace delphi_edm4hep::simulation
