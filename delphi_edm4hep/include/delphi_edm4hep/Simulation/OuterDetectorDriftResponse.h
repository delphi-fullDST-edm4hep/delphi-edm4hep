#pragma once

#include <optional>

namespace delphi_edm4hep::simulation {

class OuterDetectorDriftResponse {
public:
  double driftTimeNs(double distanceCm, double angleRadians) const;
  std::optional<double> distanceCm(double driftTimeNs,
                                   double angleRadians,
                                   double maximumDistanceCm) const;
};

} // namespace delphi_edm4hep::simulation
