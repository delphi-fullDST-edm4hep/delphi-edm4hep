#include "delphi_edm4hep/Simulation/TpcPadResponse.h"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using delphi_edm4hep::simulation::TpcPadResponse;
using delphi_edm4hep::simulation::TpcPadRow;
using delphi_edm4hep::simulation::TpcReadoutGeometry;
using delphi_edm4hep::simulation::TpcSectorTransform;

namespace {

bool close(double left, double right, double tolerance = 1e-12) {
  return std::abs(left - right) <= tolerance;
}

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main() {
  constexpr unsigned int padCount = 64;
  constexpr double radiusCm = 50.0;
  constexpr double padHeightCm = 0.5;
  constexpr double padWidthCm = 0.5;
  const auto deltaPhi = (std::numbers::pi / 3.0) / padCount;
  const auto centrePhi = -0.5 * deltaPhi;
  TpcReadoutGeometry readout(
      std::vector<TpcPadRow>{{1, padCount, radiusCm, padHeightCm, padWidthCm}},
      std::vector<TpcSectorTransform>{{1, 0, 1, 0, 0, 0}}, 145.0);
  TpcPadResponse response(std::move(readout));

  const auto signals = response.induce(
      radiusCm * std::sin(centrePhi), radiusCm * std::cos(centrePhi), 145.0,
      0.0, 1.0, 10.0);
  require(signals.size() == 5, "STAMPA must induce at most five pad signals");
  require(signals.front().address.pad == 30, "wrong first neighbor pad");
  require(signals.back().address.pad == 34, "wrong last neighbor pad");
  const auto normalization =
      0.5 * padHeightCm * padWidthCm /
      (std::numbers::pi * 2.0 * 0.37 * 0.37);
  require(signals[2].address.pad == 32, "wrong central pad");
  require(close(signals[2].response, normalization),
          "wrong central-pad normalization");
  require(close(signals[2].signal, 10.0 * normalization),
          "wrong induced central-pad signal");
  require(close(signals[0].response, signals[4].response),
          "two-pad response must be symmetric");
  require(close(signals[1].response, signals[3].response),
          "one-pad response must be symmetric");
  require(signals[2].response > signals[1].response,
          "central response must exceed adjacent response");
  require(signals[1].response > signals[0].response,
          "adjacent response must exceed outer response");

  require(response.induce(radiusCm * std::sin(centrePhi),
                          radiusCm * std::cos(centrePhi), 146.0, 0.0, 1.0,
                          10.0)
              .empty(),
          "signals beyond the drift half-length must be rejected");
  require(response.induce(radiusCm * std::sin(centrePhi),
                          radiusCm * std::cos(centrePhi), 145.0, 0.0, 1.0,
                          0.0)
              .empty(),
          "zero input signal must not induce pads");
  require(response.induce(radiusCm * std::sin(centrePhi),
                          radiusCm * std::cos(centrePhi), 145.0, 1.0, 0.0,
                          10.0)
              .size() == 5,
          "legacy tangential incidence must remain defined");
}
