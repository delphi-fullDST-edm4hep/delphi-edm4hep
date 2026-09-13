#include "delphi_edm4hep/Reconstruction/HelixTrajectory.h"

#include <cmath>
#include <stdexcept>

int main() {
  using delphi_edm4hep::reconstruction::HelixTrajectory;
  constexpr double radius = 10000.0;
  constexpr double z0 = -2.5;
  constexpr double slope = 0.35;
  HelixTrajectory trajectory(0.0, 0.0, -1.0 / radius, z0, slope);
  if (!trajectory.valid()) {
    throw std::runtime_error("valid helix was rejected");
  }
  for (unsigned int index = 1; index <= 16; ++index) {
    const auto angle = 0.003 * index;
    const auto x = radius * std::sin(angle);
    const auto y = radius * (1.0 - std::cos(angle));
    const auto path = radius * angle;
    const auto result = trajectory.at(x, y, z0 + slope * path);
    if (std::abs(result.pathLengthMm - path) > 1e-8 ||
        std::abs(result.transverseResidualMm) > 1e-8 ||
        std::abs(result.longitudinalResidualMm) > 1e-8) {
      throw std::runtime_error("helix surface prediction changed");
    }
  }
}
