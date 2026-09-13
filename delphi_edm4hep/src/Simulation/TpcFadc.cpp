#include "delphi_edm4hep/Simulation/TpcFadc.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace delphi_edm4hep::simulation {

TpcFadc::TpcFadc(TpcFadcParameters parameters) : parameters_(parameters) {
  if (parameters_.pedestalSigmaCounts < 0 ||
      parameters_.commonNoiseFraction < 0 ||
      parameters_.commonNoiseFraction > 1 || parameters_.thresholdCounts == 0 ||
      parameters_.saturationCount > 255 || parameters_.maximumClusters == 0) {
    throw std::runtime_error("invalid TPC FADC parameters");
  }
}

std::vector<std::uint8_t> TpcFadc::digitize(
    const std::vector<double> &analog,
    const TpcPadElectronicsCalibration &calibration,
    double commonNormalDeviate,
    const std::vector<double> &pixelNormalDeviates) const {
  if (analog.size() != pixelNormalDeviates.size()) {
    throw std::runtime_error("TPC FADC noise vector has the wrong size");
  }
  if (calibration.lowRangeSignalPerCount <= 0 || calibration.gainRatio <= 0 ||
      std::abs(calibration.gainRatio - 1.0) < 1e-12) {
    throw std::runtime_error("invalid TPC pad calibration for FADC");
  }

  const auto commonNoise = commonNormalDeviate *
                           std::sqrt(parameters_.commonNoiseFraction) *
                           parameters_.pedestalSigmaCounts;
  const auto independentSigma =
      parameters_.pedestalSigmaCounts *
      std::sqrt(1.0 - parameters_.commonNoiseFraction);
  const auto lowPedestal = calibration.pedestalCounts + commonNoise;
  const auto lowSlope = calibration.lowRangeSignalPerCount;
  const auto highSlope = lowSlope * calibration.gainRatio;
  const auto highPedestal =
      192.0 * (1.0 - 1.0 / calibration.gainRatio) +
      lowPedestal / calibration.gainRatio;
  const auto rangeBreak = lowSlope * highSlope *
                          (highPedestal - lowPedestal) /
                          (highSlope - lowSlope);

  std::vector<std::uint8_t> result;
  result.reserve(analog.size());
  for (std::size_t index = 0; index < analog.size(); ++index) {
    const auto code = analog[index] <= rangeBreak
                          ? analog[index] / lowSlope + lowPedestal
                          : analog[index] / highSlope + highPedestal;
    const auto noisy = static_cast<int>(
        code + pixelNormalDeviates[index] * independentSigma);
    result.push_back(static_cast<std::uint8_t>(std::clamp(
        noisy, 0, static_cast<int>(parameters_.saturationCount))));
  }
  return result;
}

std::vector<TpcFadcCluster>
TpcFadc::zeroSuppress(unsigned int firstBin,
                      const std::vector<std::uint8_t> &samples) const {
  std::vector<TpcFadcCluster> result;
  for (std::size_t index = 0; index < samples.size(); ++index) {
    if (samples[index] < parameters_.thresholdCounts) {
      continue;
    }
    const auto start = index > parameters_.pastBins
                           ? index - parameters_.pastBins
                           : std::size_t{0};
    const auto stop = std::min(samples.size() - 1,
                               index + parameters_.futureBins);
    if (!result.empty()) {
      auto &last = result.back();
      const auto lastStop =
          static_cast<std::size_t>(last.firstBin - firstBin) +
          last.samples.size() - 1;
      if (start <= lastStop + 1) {
        if (stop > lastStop) {
          last.samples.insert(last.samples.end(),
                              samples.begin() + lastStop + 1,
                              samples.begin() + stop + 1);
        }
        continue;
      }
    }
    if (result.size() == parameters_.maximumClusters) {
      break;
    }
    result.push_back(
        {firstBin + static_cast<unsigned int>(start),
         std::vector<std::uint8_t>(samples.begin() + start,
                                   samples.begin() + stop + 1)});
  }
  return result;
}

} // namespace delphi_edm4hep::simulation
