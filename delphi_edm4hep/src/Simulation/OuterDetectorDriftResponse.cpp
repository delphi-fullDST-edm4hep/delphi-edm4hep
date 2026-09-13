#include "delphi_edm4hep/Simulation/OuterDetectorDriftResponse.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace delphi_edm4hep::simulation {
namespace {

double polynomial(const std::array<double, 6> &coefficients, double x) {
  auto result = coefficients.back();
  for (auto index = coefficients.size() - 1; index-- > 0;) {
    result = coefficients[index] + result * x;
  }
  return result;
}

constexpr std::array<double, 6> degree0{0.002273, 0.184770, 0.0051005,
                                        -0.0020837, 0.0003832, -0.0000186};
constexpr std::array<double, 6> degree30{-0.0109933, 0.2394714, -0.0432895,
                                         0.0138022, -0.0018324, 0.0000916};
constexpr std::array<double, 6> degree45{-0.0159508, 0.2557405, -0.0564483,
                                         0.0171278, -0.0021296, 0.0001009};
constexpr std::array<double, 6> degree60{-0.0104794, 0.2500042, -0.0514603,
                                         0.0163749, -0.0022032, 0.0001153};

} // namespace

double OuterDetectorDriftResponse::driftTimeNs(double distanceCm,
                                                double angleRadians) const {
  auto angleDegrees =
      std::fmod(std::abs(angleRadians) * 180.0 / std::numbers::pi, 90.0);
  if (angleDegrees < 0) {
    angleDegrees += 90.0;
  }
  const auto distanceMm = 10.0 * distanceCm;
  if (distanceMm < 0.4) {
    return 100.0 * polynomial(degree0, 0.35);
  }
  const auto interpolate = [distanceMm](const auto &left, const auto &right,
                                         double fraction) {
    return polynomial(left, distanceMm) +
           fraction * (polynomial(right, distanceMm) -
                       polynomial(left, distanceMm));
  };
  double scaledTime{};
  if (angleDegrees < 30.0) {
    scaledTime = interpolate(degree0, degree30, angleDegrees / 30.0);
  } else if (angleDegrees < 45.0) {
    scaledTime = interpolate(degree30, degree45,
                             (angleDegrees - 30.0) / 15.0);
  } else if (angleDegrees < 60.0) {
    scaledTime = interpolate(degree45, degree60,
                             (angleDegrees - 45.0) / 15.0);
  } else {
    scaledTime = interpolate(degree60, degree0,
                             (angleDegrees - 60.0) / 30.0);
  }
  return 100.0 * scaledTime;
}

std::optional<double> OuterDetectorDriftResponse::distanceCm(
    double driftTime, double angleRadians, double maximumDistanceCm) const {
  if (!std::isfinite(driftTime) || maximumDistanceCm <= 0) {
    return std::nullopt;
  }
  const auto minimumTime = driftTimeNs(0.0, angleRadians);
  const auto maximumTime = driftTimeNs(maximumDistanceCm, angleRadians);
  if (driftTime < minimumTime - 1e-6 || driftTime > maximumTime + 1e-6) {
    return std::nullopt;
  }
  double lower = 0.04;
  double upper = maximumDistanceCm;
  if (driftTime <= driftTimeNs(lower, angleRadians)) {
    return 0.0;
  }
  for (unsigned int iteration = 0; iteration < 64; ++iteration) {
    const auto middle = 0.5 * (lower + upper);
    if (driftTimeNs(middle, angleRadians) < driftTime) {
      lower = middle;
    } else {
      upper = middle;
    }
  }
  return 0.5 * (lower + upper);
}

} // namespace delphi_edm4hep::simulation
