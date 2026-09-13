#include "delphi_edm4hep/Simulation/TpcFadc.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main() {
  delphi_edm4hep::simulation::TpcPadElectronicsCalibration calibration;
  calibration.pedestalCounts = 0;
  calibration.lowRangeSignalPerCount = 1;
  calibration.gainRatio = 5;
  const delphi_edm4hep::simulation::TpcFadc fadc;

  const std::vector<double> analog{0, 0, 19, 20, 21, 0, 0,
                                   0, 0, 0,  25, 0,  1000};
  const auto samples =
      fadc.digitize(analog, calibration, 0.0,
                    std::vector<double>(analog.size(), 0.0));
  require(samples[2] == 19 && samples[3] == 20 && samples[4] == 21,
          "low-range FADC conversion changed");
  require(samples.back() == 255, "FADC saturation changed");

  const auto clusters = fadc.zeroSuppress(81, samples);
  require(clusters.size() == 2, "wrong zero-suppressed cluster count");
  require(clusters[0].firstBin == 82 && clusters[0].samples.size() == 6,
          "wrong first threshold window");
  require(clusters[1].firstBin == 89 && clusters[1].samples.size() == 5,
          "wrong second threshold window");

  const auto noisy = fadc.digitize({0}, calibration, 0.0, {-10.0});
  require(noisy.front() == 0, "negative FADC code was not clamped");
}
