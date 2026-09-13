#include "delphi_edm4hep/Simulation/TpcTimeResponse.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace delphi_edm4hep::simulation {

TpcTimeResponse::TpcTimeResponse(TpcTimeResponseParameters parameters)
    : parameters_(parameters) {
  if (parameters_.timeBinMicroseconds <= 0 ||
      parameters_.longitudinalDiffusionCmPerSqrtCm <= 0 ||
      parameters_.testLongitudinalSigmaMicroseconds < 0 ||
      parameters_.electronicsHwhmMicroseconds <= 0 ||
      parameters_.halfWindowBins == 0 || parameters_.maximumBins == 0) {
    throw std::runtime_error("invalid TPC time-response parameters");
  }
}

double TpcTimeResponse::longitudinalVariance(
    double zCm, double deltaZCm, double driftHalfLengthCm,
    double driftVelocityCmPerMicrosecond) const {
  if (driftHalfLengthCm <= 0 || driftVelocityCmPerMicrosecond <= 0) {
    throw std::runtime_error("invalid TPC drift geometry or velocity");
  }
  const auto driftCm = driftHalfLengthCm - std::abs(zCm);
  if (driftCm < 0) {
    return 0;
  }
  const auto velocitySquared =
      driftVelocityCmPerMicrosecond * driftVelocityCmPerMicrosecond;
  const auto diffusion =
      std::pow(parameters_.longitudinalDiffusionCmPerSqrtCm, 2) *
      (driftCm - parameters_.referenceDriftCm) / velocitySquared;
  const auto testSpread =
      std::pow(parameters_.testLongitudinalSigmaMicroseconds, 2);
  const auto stepSpread = deltaZCm * deltaZCm / (12.0 * velocitySquared);
  // STSIM stores SIGLGT=ABS(SIGLD2B+SIGTE2)+SIGTET. The absolute value
  // surrounds the diffusion and test-spread sum, which matters in the final
  // 6.7 cm of drift where the fitted diffusion term is negative.
  return std::abs(diffusion + testSpread) + stepSpread;
}

TpcSampledSignal TpcTimeResponse::sample(
    double zCm, double deltaZCm, double driftHalfLengthCm,
    double driftVelocityCmPerMicrosecond, double signal,
    double phaseNormalDeviate, double asymmetryNormalDeviate) const {
  if (signal < 0) {
    throw std::runtime_error("TPC time-response signal must be non-negative");
  }
  if (std::abs(phaseNormalDeviate) > 5.0 ||
      std::abs(asymmetryNormalDeviate) > 4.0) {
    throw std::runtime_error("TPC time-response Gaussian draw is untruncated");
  }
  const auto driftCm = driftHalfLengthCm - std::abs(zCm);
  if (signal == 0 || driftCm < 0) {
    return {};
  }
  if (driftVelocityCmPerMicrosecond <= 0) {
    throw std::runtime_error("invalid TPC drift velocity");
  }

  const auto interval = parameters_.timeBinMicroseconds;
  const auto phaseTime =
      parameters_.phaseSigmaBins * phaseNormalDeviate * interval;
  const auto driftTime = std::abs(driftCm / driftVelocityCmPerMicrosecond);
  const auto centreBin =
      static_cast<int>(std::lround((driftTime - phaseTime) / interval));
  const auto availableBins = std::min(
      static_cast<unsigned int>(driftHalfLengthCm /
                                driftVelocityCmPerMicrosecond / interval),
      parameters_.maximumBins);
  const auto first = std::max(1, centreBin -
                                    static_cast<int>(parameters_.halfWindowBins));
  const auto last = std::min(static_cast<int>(availableBins),
                             centreBin +
                                 static_cast<int>(parameters_.halfWindowBins));
  if (last <= first) {
    return {};
  }

  const auto longitudinalVarianceUs2 =
      longitudinalVariance(zCm, deltaZCm, driftHalfLengthCm,
                           driftVelocityCmPerMicrosecond);
  const auto electronicsSigma =
      parameters_.electronicsHwhmMicroseconds / std::sqrt(2.0 * std::log(2.0));
  const auto electronicsVariance = electronicsSigma * electronicsSigma;
  const auto totalVariance = longitudinalVarianceUs2 + electronicsVariance;
  const auto gaussianCoefficient = 1.0 / (2.0 * totalVariance);
  const auto normalizedSignal =
      signal * std::sqrt(electronicsVariance / totalVariance);
  const auto binCoefficient = gaussianCoefficient * interval * interval;
  const auto asymmetry =
      0.055 * binCoefficient * (1.0 + 0.60 * asymmetryNormalDeviate);
  const auto varianceInBins = totalVariance / (interval * interval);
  const auto normalization =
      1.0 + asymmetry * asymmetry * std::pow(varianceInBins, 3) * 15.0 / 2.0;

  TpcSampledSignal result;
  result.firstBin = static_cast<unsigned int>(first);
  result.intervalMicroseconds = interval;
  result.driftTimeMicroseconds = driftTime;
  result.longitudinalVarianceMicroseconds2 = longitudinalVarianceUs2;
  result.amplitudes.reserve(static_cast<std::size_t>(last - first + 1));
  for (auto bin = first; bin <= last; ++bin) {
    const auto time = phaseTime + static_cast<double>(bin) * interval;
    const auto offset = time - driftTime;
    const auto asymmetricArgument = asymmetry * std::pow(offset / interval, 3);
    const auto asymmetricShape =
        1.0 + asymmetricArgument +
        asymmetricArgument * asymmetricArgument / 2.0 +
        asymmetricArgument * asymmetricArgument * asymmetricArgument / 6.0;
    const auto amplitude =
        normalizedSignal *
        (parameters_.baseline +
         std::exp(-offset * offset * gaussianCoefficient) * asymmetricShape /
             normalization);
    result.amplitudes.push_back(std::max(0.0, amplitude));
  }
  return result;
}

} // namespace delphi_edm4hep::simulation
