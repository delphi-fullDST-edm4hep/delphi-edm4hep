#include "delphi_edm4hep/Simulation/VertexChannelResponse.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool close(double left, double right, double tolerance = 1.0e-9) {
  return std::abs(left - right) < tolerance;
}

} // namespace

int main() {
  using namespace delphi_edm4hep::simulation;
  const VertexChannelResponse response;

  // 36 keV in silicon is exactly 10,000 electron-hole pairs at 3.6 eV.
  const auto closerP = response.digitize(36.0e-6, VertexBarrelLayer::Closer,
                                         VertexReadoutSide::P, 0.5);
  require(close(closerP.signalElectrons, 11250.0),
          "energy/noise conversion is wrong");
  require(close(closerP.signalAdc, 11.25) &&
              closerP.packedSignalQuarterAdc == 45,
          "SVPACK/SVFORM signal conversion is wrong");
  require(close(closerP.noiseAdc, 2.5) && closerP.packedNoiseQuarterAdc == 10,
          "SVPACK/SVFORM noise conversion is wrong");
  require(!closerP.aboveSingleChannelThreshold,
          "closer P threshold should be 12,500 electrons");

  const auto outerP = response.digitize(36.0e-6, VertexBarrelLayer::Outer,
                                        VertexReadoutSide::P, 0.0);
  require(!outerP.aboveSingleChannelThreshold,
          "outer P threshold should be 12,500 electrons");
  require(close(outerP.chargeFc, 1.602176634, 1.0e-12),
          "integrated charge conversion is wrong");

  const auto clipped = response.digitize(0.0, VertexBarrelLayer::Inner,
                                         VertexReadoutSide::N, -10.0);
  require(clipped.signalElectrons == 0 && clipped.packedSignalQuarterAdc == 0,
          "negative noisy charge was not clipped");

  std::cout << "Vertex channel response closure passed\n";
}
