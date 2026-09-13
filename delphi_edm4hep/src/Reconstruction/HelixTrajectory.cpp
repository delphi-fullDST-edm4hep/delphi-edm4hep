#include "delphi_edm4hep/Reconstruction/HelixTrajectory.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace delphi_edm4hep::reconstruction {

HelixTrajectory::HelixTrajectory(double d0Mm, double phiRadians,
                                 double omegaPerMm, double z0Mm,
                                 double tanLambda)
    : z0Mm_(z0Mm), tanLambda_(tanLambda) {
  if (!std::isfinite(d0Mm) || !std::isfinite(phiRadians) ||
      !std::isfinite(omegaPerMm) || !std::isfinite(z0Mm) ||
      !std::isfinite(tanLambda) || std::abs(omegaPerMm) < 1e-15) {
    return;
  }
  orientation_ = omegaPerMm < 0 ? 1 : -1;
  radiusMm_ = 1.0 / std::abs(omegaPerMm);
  const auto perigeeX = -d0Mm * std::sin(phiRadians);
  const auto perigeeY = d0Mm * std::cos(phiRadians);
  centerX_ = perigeeX - orientation_ * radiusMm_ * std::sin(phiRadians);
  centerY_ = perigeeY + orientation_ * radiusMm_ * std::cos(phiRadians);
  perigeeAngle_ = std::atan2(perigeeY - centerY_, perigeeX - centerX_);
  valid_ = true;
}

HelixTrajectoryPoint HelixTrajectory::at(double xMm, double yMm,
                                         double zMm) const {
  if (!valid_) {
    throw std::logic_error("invalid helix trajectory");
  }
  const auto dx = xMm - centerX_;
  const auto dy = yMm - centerY_;
  const auto distance = std::hypot(dx, dy);
  const auto angle = std::atan2(dy, dx);
  auto directedAngle = orientation_ * std::remainder(
                                           angle - perigeeAngle_,
                                           2.0 * std::numbers::pi);
  if (directedAngle < 0) {
    directedAngle += 2.0 * std::numbers::pi;
  }
  const auto pathLength = radiusMm_ * directedAngle;
  const auto predictedZ = z0Mm_ + tanLambda_ * pathLength;
  return {pathLength, distance - radiusMm_, zMm - predictedZ, predictedZ};
}

} // namespace delphi_edm4hep::reconstruction
