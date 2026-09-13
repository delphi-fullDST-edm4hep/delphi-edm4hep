#pragma once

#include <vector>

namespace delphi_edm4hep::simulation {

struct TpcTimeResponseParameters {
  double timeBinMicroseconds{0.07382};
  double longitudinalDiffusionCmPerSqrtCm{0.036};
  double referenceDriftCm{6.7};
  double testLongitudinalSigmaMicroseconds{0.019};
  double electronicsHwhmMicroseconds{0.090};
  double baseline{-0.006};
  double phaseSigmaBins{0.12};
  unsigned int halfWindowBins{6};
  unsigned int maximumBins{400};
};

struct TpcSampledSignal {
  unsigned int firstBin{};
  double intervalMicroseconds{};
  double driftTimeMicroseconds{};
  double longitudinalVarianceMicroseconds2{};
  std::vector<double> amplitudes;
};

class TpcTimeResponse {
public:
  explicit TpcTimeResponse(
      TpcTimeResponseParameters parameters = TpcTimeResponseParameters{});

  double longitudinalVariance(double zCm, double deltaZCm,
                              double driftHalfLengthCm,
                              double driftVelocityCmPerMicrosecond) const;

  // phaseNormalDeviate and asymmetryNormalDeviate are the truncated Gaussian
  // draws made by STDIPW. Supplying them explicitly keeps this response model
  // deterministic and leaves random-engine ownership to the framework module.
  TpcSampledSignal sample(double zCm, double deltaZCm,
                          double driftHalfLengthCm,
                          double driftVelocityCmPerMicrosecond, double signal,
                          double phaseNormalDeviate,
                          double asymmetryNormalDeviate) const;

  const TpcTimeResponseParameters &parameters() const { return parameters_; }

private:
  TpcTimeResponseParameters parameters_;
};

} // namespace delphi_edm4hep::simulation
