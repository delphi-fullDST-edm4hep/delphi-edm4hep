#include "delphi_edm4hep/Simulation/VertexDigitizationConditions.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool close(double left, double right) {
  return std::abs(left - right) < 1.0e-12;
}

} // namespace

int main() {
  using namespace delphi_edm4hep::simulation;

  const auto conditions = VertexDigitizationConditions::legacyV94c();
  const auto &closer = conditions.layer(VertexBarrelLayer::Closer);
  const auto &inner = conditions.layer(VertexBarrelLayer::Inner);
  const auto &outer = conditions.layer(VertexBarrelLayer::Outer);

  require(closer.modules == 24 && inner.modules == 24 && outer.modules == 24 &&
              closer.physicalPlaquettesPerModule == 4 &&
              inner.physicalPlaquettesPerModule == 4 &&
              outer.physicalPlaquettesPerModule == 4,
          "barrel topology differs from v94c SVCALB");

  const auto closerCentral =
      conditions.plaquette(VertexBarrelLayer::Closer, 1, 2);
  const auto closerPeripheral =
      conditions.plaquette(VertexBarrelLayer::Closer, 1, 1);
  require(closerCentral.pReadoutChannels == 384 &&
              closerCentral.pPhysicalStrips == 768 &&
              close(closerCentral.pReadoutPitchCm, 0.0050) &&
              close(closerCentral.pPhysicalPitchCm, 0.0025),
          "closer P topology differs from v94c SVCALB/SVINI");
  require(closerCentral.nReadoutChannels == 1152 &&
              closerCentral.nFirstPitchChannels == 768 &&
              close(closerCentral.nFirstPitchCm, 0.00495) &&
              close(closerCentral.nSecondPitchCm, 0.00990) &&
              close(closerCentral.nSecondZoneOffsetCm, -0.0025) &&
              closerPeripheral.nReadoutChannels == 384 &&
              close(closerPeripheral.nFirstPitchCm, 0.0150),
          "closer N pitch zones differ from v94c SVCALB/SVAAR");

  const auto innerOdd = conditions.plaquette(VertexBarrelLayer::Inner, 1, 2);
  const auto innerEven = conditions.plaquette(VertexBarrelLayer::Inner, 2, 2);
  require(innerOdd.pReadoutChannels == 512 &&
              innerEven.pReadoutChannels == 640 && !innerOdd.nReadoutEnabled(),
          "inner odd/even readout differs from v94c SVCALB");

  const auto outerCentral =
      conditions.plaquette(VertexBarrelLayer::Outer, 1, 3);
  const auto outerPeripheral =
      conditions.plaquette(VertexBarrelLayer::Outer, 1, 4);
  require(outerCentral.nReadoutChannels == 1280 &&
              close(outerCentral.nFirstPitchCm, 0.00420) &&
              outerPeripheral.nReadoutChannels == 640 &&
              close(outerPeripheral.nFirstPitchCm, 0.00840),
          "outer N topology differs from v94c SVCALB/SVAAR");

  require(close(closer.pNoiseElectrons, 2500.0) &&
              close(closer.nNoiseElectrons, 2500.0) &&
              close(inner.pNoiseElectrons, 1700.0) &&
              close(inner.nNoiseElectrons, 0.0) &&
              close(outer.pNoiseElectrons, 2500.0) &&
              close(outer.nNoiseElectrons, 2500.0),
          "VD noise zones differ from v94c SVINI");
  require(close(closer.pThresholdSigma, 5.0) &&
              close(outer.nThresholdSigma, 5.0),
          "VD thresholds differ from v94c SVINI");
  require(close(conditions.trackingStepCm(), 0.001) &&
              conditions.minimumActiveSteps() == 3 &&
              close(conditions.electronsPerAdc(), 1000.0) &&
              close(conditions.lorentzShiftCm(), 0.0008),
          "VD global response values differ from v94c VDSIM");
  require(!conditions.crossTalkEnabled() && conditions.noiseClustersEnabled() &&
              conditions.minimumNoiseClusterSize() == 2,
          "VD default response switches differ from v94c SVINI");
  require(close(conditions.crossTalkFractions()[0], 0.7490) &&
              close(conditions.crossTalkFractions()[3], 0.0078),
          "VD P-side cross-talk kernel differs from v94c SVINI");

  std::cout << "Vertex v94c digitization conditions closure passed\n";
}
