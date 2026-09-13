#include "delphi_edm4hep/Simulation/TpcTimeResponse.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool close(double left, double right, double tolerance = 1e-12) {
  return std::abs(left - right) <= tolerance;
}

} // namespace

int main() {
  const delphi_edm4hep::simulation::TpcTimeResponse response;
  const auto variance = response.longitudinalVariance(100.0, 1.0, 145.0, 7.0);
  const auto expectedVariance =
      std::abs(0.036 * 0.036 * (45.0 - 6.7) / (7.0 * 7.0)) +
      0.019 * 0.019 + 1.0 / (12.0 * 7.0 * 7.0);
  require(close(variance, expectedVariance),
          "STDIFF longitudinal variance changed");
  const auto nearEndplate =
      response.longitudinalVariance(144.0, 0.0, 145.0, 7.0);
  const auto expectedNearEndplate =
      std::abs(0.036 * 0.036 * (1.0 - 6.7) / (7.0 * 7.0) +
               0.019 * 0.019);
  require(close(nearEndplate, expectedNearEndplate),
          "STDIFF near-endplate absolute value changed");

  const auto sampled = response.sample(100.0, 1.0, 145.0, 7.0, 100.0, 0.0, 0.0);
  require(sampled.firstBin == 81, "wrong first STDIPW time bin");
  require(sampled.amplitudes.size() == 13, "wrong STDIPW time window");
  const auto peak = std::max_element(sampled.amplitudes.begin(),
                                     sampled.amplitudes.end());
  require(peak != sampled.amplitudes.end(), "missing time-response peak");
  require(sampled.firstBin +
              static_cast<unsigned int>(peak - sampled.amplitudes.begin()) ==
              87,
          "wrong STDIPW peak bin");
  require(sampled.amplitudes.front() < *peak && sampled.amplitudes.back() < *peak,
          "time response does not peak inside its window");
  require(response.sample(146.0, 0.0, 145.0, 7.0, 1.0, 0.0, 0.0)
              .amplitudes.empty(),
          "time response outside the drift volume was accepted");
}
