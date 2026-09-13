#include "delphi_edm4hep/Reconstruction/CentralTrackFit.h"

#include <cmath>
#include <stdexcept>
#include <vector>

int main() {
  using namespace delphi_edm4hep::reconstruction;
  constexpr double radiusMm = 10000.0;
  constexpr double tanLambda = 0.35;
  constexpr double z0Mm = -2.5;
  std::vector<SpacePoint> points;
  for (unsigned int index = 1; index <= 16; ++index) {
    const auto arcAngle = 0.003 * index;
    const auto angle = -std::acos(-1.0) / 2.0 + arcAngle;
    points.push_back({radiusMm * std::cos(angle),
                      radiusMm + radiusMm * std::sin(angle),
                      z0Mm + tanLambda * radiusMm * arcAngle});
  }
  const auto fit = fitCentralTrack(points, 0.1, 0.1);
  if (!fit || std::abs(fit->d0Mm) > 1e-6 ||
      std::abs(fit->phiRadians) > 1e-6 ||
      std::abs(fit->omegaPerMm + 1.0 / radiusMm) > 1e-10 ||
      std::abs(fit->z0Mm - z0Mm) > 1e-6 ||
      std::abs(fit->tanLambda - tanLambda) > 1e-9 || fit->ndf != 27 ||
      fit->chi2 > 1e-10) {
    throw std::runtime_error("native central helix fit changed");
  }
  const auto constrained = fitCentralTrack(points, 0.1, 0.1, true);
  if (!constrained || std::abs(constrained->d0Mm) > 1e-12 ||
      std::abs(constrained->omegaPerMm + 1.0 / radiusMm) > 1e-10 ||
      std::abs(constrained->z0Mm - z0Mm) > 1e-6) {
    throw std::runtime_error("IP-constrained central helix fit changed");
  }

  std::vector<SpacePointMeasurement> planarMeasurements;
  for (std::size_t index = 0; index < points.size(); ++index) {
    auto point = points[index];
    std::optional<double> longitudinalSigma = 0.1;
    if (index % 2 == 1) {
      // ID-like planar measurements carry no useful z coordinate. Their
      // placeholder must not bias the longitudinal helix fit.
      point.zMm = 10000.0;
      longitudinalSigma = std::nullopt;
    }
    planarMeasurements.push_back({point, 0.1, longitudinalSigma});
  }
  const auto planar = fitCentralTrackMeasurements(planarMeasurements, true);
  if (!planar || std::abs(planar->omegaPerMm + 1.0 / radiusMm) > 1e-10 ||
      std::abs(planar->z0Mm - z0Mm) > 1e-6 ||
      std::abs(planar->tanLambda - tanLambda) > 1e-9 || planar->ndf != 19) {
    throw std::runtime_error("partial-coordinate central fit changed");
  }

  constexpr double shallowRadiusMm = 27000.0;
  std::vector<SpacePoint> quantizedPoints;
  std::vector<SpacePointMeasurement> quantizedMeasurements;
  for (unsigned int index = 1; index <= 16; ++index) {
    const auto arcAngle = 0.0017 * index;
    SpacePoint point{shallowRadiusMm * std::sin(arcAngle),
                     shallowRadiusMm * (1.0 - std::cos(arcAngle)),
                     z0Mm + tanLambda * shallowRadiusMm * arcAngle};
    point.xMm = 5.0 * std::round(point.xMm / 5.0);
    point.yMm = 5.0 * std::round(point.yMm / 5.0);
    quantizedPoints.push_back(point);
    quantizedMeasurements.push_back({point, 5.0, 10.0});
  }
  const auto algebraic = fitCentralTrack(quantizedPoints, 5.0, 10.0, true);
  const auto geometric =
      fitCentralTrackMeasurements(quantizedMeasurements, true);
  const auto expectedOmega = -1.0 / shallowRadiusMm;
  if (!algebraic || !geometric ||
      std::abs(geometric->omegaPerMm - expectedOmega) >=
          std::abs(algebraic->omegaPerMm - expectedOmega)) {
    throw std::runtime_error("geometric circle refinement lost its closure");
  }
}
