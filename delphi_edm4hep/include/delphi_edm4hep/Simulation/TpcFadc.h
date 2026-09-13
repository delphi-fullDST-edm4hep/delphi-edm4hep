#pragma once

#include "delphi_edm4hep/Simulation/TpcDigitizationConditions.h"

#include <cstdint>
#include <vector>

namespace delphi_edm4hep::simulation {

struct TpcFadcParameters {
  double pedestalSigmaCounts{2.3};
  double commonNoiseFraction{0.78};
  unsigned int thresholdCounts{20};
  unsigned int pastBins{2};
  unsigned int futureBins{2};
  unsigned int saturationCount{255};
  unsigned int maximumClusters{20};
};

struct TpcFadcCluster {
  unsigned int firstBin{};
  std::vector<std::uint8_t> samples;
};

class TpcFadc {
public:
  explicit TpcFadc(TpcFadcParameters parameters = TpcFadcParameters{});

  std::vector<std::uint8_t>
  digitize(const std::vector<double> &analog,
           const TpcPadElectronicsCalibration &calibration,
           double commonNormalDeviate,
           const std::vector<double> &pixelNormalDeviates) const;

  std::vector<TpcFadcCluster>
  zeroSuppress(unsigned int firstBin,
               const std::vector<std::uint8_t> &samples) const;

private:
  TpcFadcParameters parameters_;
};

} // namespace delphi_edm4hep::simulation
