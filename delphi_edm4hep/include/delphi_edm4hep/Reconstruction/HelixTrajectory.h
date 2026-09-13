#pragma once

namespace delphi_edm4hep::reconstruction {

struct HelixTrajectoryPoint {
  double pathLengthMm{};
  double transverseResidualMm{};
  double longitudinalResidualMm{};
  double predictedZMm{};
};

class HelixTrajectory {
public:
  HelixTrajectory(double d0Mm, double phiRadians, double omegaPerMm,
                  double z0Mm, double tanLambda);

  bool valid() const { return valid_; }
  HelixTrajectoryPoint at(double xMm, double yMm, double zMm) const;

private:
  double centerX_{};
  double centerY_{};
  double radiusMm_{};
  double perigeeAngle_{};
  double z0Mm_{};
  double tanLambda_{};
  int orientation_{};
  bool valid_{};
};

} // namespace delphi_edm4hep::reconstruction
