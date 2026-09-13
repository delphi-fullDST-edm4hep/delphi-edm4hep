#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace delphi_edm4hep::simulation {

enum class VertexBarrelLayer : std::uint8_t {
  Closer = 1,
  Inner = 2,
  Outer = 3,
};

enum class VertexLongitudinalRegion : std::uint8_t {
  Central = 0,
  Peripheral = 1,
};

enum class VertexReadoutSide : std::uint8_t { P = 0, N = 1 };

// Readout topology for one physical silicon plaquette. VDSIM 4.6 models the
// P side with an intermediate floating strip, hence the physical pitch is
// half the readout pitch. The closer-layer central N side has two pitch zones;
// all other N layouts use only the first zone.
struct VertexPlaquetteConditions {
  std::uint32_t pReadoutChannels{};
  std::uint32_t pPhysicalStrips{};
  double pReadoutPitchCm{};
  double pPhysicalPitchCm{};
  std::uint32_t nReadoutChannels{};
  std::uint32_t nFirstPitchChannels{};
  double nFirstPitchCm{};
  double nSecondPitchCm{};
  double nSecondZoneOffsetCm{};
  VertexLongitudinalRegion longitudinalRegion{};

  bool nReadoutEnabled() const { return nReadoutChannels != 0; }
};

struct VertexLayerConditions {
  std::uint32_t modules{};
  std::uint32_t physicalPlaquettesPerModule{};
  double pNoiseElectrons{};
  double nNoiseElectrons{};
  double pThresholdSigma{};
  double nThresholdSigma{};
};

class VertexDigitizationConditions {
public:
  // Defaults hard-coded by VDSIM 4.6 in the v94c release (SVCALB/SVINI).
  static VertexDigitizationConditions legacyV94c();

  const VertexLayerConditions &layer(VertexBarrelLayer layer) const;
  VertexPlaquetteConditions plaquette(VertexBarrelLayer layer,
                                      std::size_t module,
                                      std::size_t physicalPlaquette) const;
  static VertexLongitudinalRegion
  longitudinalRegion(std::size_t physicalPlaquette);

  double trackingStepCm() const { return trackingStepCm_; }
  std::uint32_t minimumActiveSteps() const { return minimumActiveSteps_; }
  double electronsPerAdc() const { return electronsPerAdc_; }
  double electronHoleEnergyEv() const { return electronHoleEnergyEv_; }
  double lorentzShiftCm() const { return lorentzShiftCm_; }
  bool crossTalkEnabled() const { return crossTalkEnabled_; }
  bool noiseClustersEnabled() const { return noiseClustersEnabled_; }
  std::uint32_t minimumNoiseClusterSize() const {
    return minimumNoiseClusterSize_;
  }
  const std::array<double, 4> &crossTalkFractions() const {
    return crossTalkFractions_;
  }

private:
  std::array<VertexLayerConditions, 3> layers_;
  double trackingStepCm_{0.001};
  std::uint32_t minimumActiveSteps_{3};
  double electronsPerAdc_{1000.0};
  // Geant4 bridge into VDSIM's electron-domain pulse calibration.
  double electronHoleEnergyEv_{3.6};
  double lorentzShiftCm_{0.0008};
  bool crossTalkEnabled_{false};
  bool noiseClustersEnabled_{true};
  std::uint32_t minimumNoiseClusterSize_{2};
  std::array<double, 4> crossTalkFractions_{0.7490, 0.0991, 0.0180, 0.0078};
};

} // namespace delphi_edm4hep::simulation
