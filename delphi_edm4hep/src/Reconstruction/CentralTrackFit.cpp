#include "delphi_edm4hep/Reconstruction/CentralTrackFit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace delphi_edm4hep::reconstruction {
namespace {

double wrapped(double value) {
  return std::remainder(value, 2.0 * std::numbers::pi);
}

std::optional<std::array<double, 3>> solve3x3(
    std::array<std::array<double, 4>, 3> matrix) {
  for (std::size_t column = 0; column < 3; ++column) {
    auto pivot = column;
    for (auto row = column + 1; row < 3; ++row) {
      if (std::abs(matrix[row][column]) >
          std::abs(matrix[pivot][column])) {
        pivot = row;
      }
    }
    if (std::abs(matrix[pivot][column]) < 1e-12) {
      return std::nullopt;
    }
    std::swap(matrix[pivot], matrix[column]);
    const auto scale = matrix[column][column];
    for (auto entry = column; entry < 4; ++entry) {
      matrix[column][entry] /= scale;
    }
    for (std::size_t row = 0; row < 3; ++row) {
      if (row == column) {
        continue;
      }
      const auto factor = matrix[row][column];
      for (auto entry = column; entry < 4; ++entry) {
        matrix[row][entry] -= factor * matrix[column][entry];
      }
    }
  }
  return std::array<double, 3>{matrix[0][3], matrix[1][3], matrix[2][3]};
}

} // namespace

std::optional<CentralTrackFitResult>
fitCentralTrack(const std::vector<SpacePoint> &input,
                double transverseSigmaMm, double longitudinalSigmaMm,
                bool constrainToInteractionPoint) {
  if (transverseSigmaMm <= 0 || longitudinalSigmaMm <= 0) {
    return std::nullopt;
  }
  std::vector<SpacePointMeasurement> measurements;
  measurements.reserve(input.size());
  for (const auto &point : input) {
    measurements.push_back(
        {point, transverseSigmaMm, longitudinalSigmaMm});
  }
  return fitCentralTrackMeasurements(measurements,
                                     constrainToInteractionPoint, false);
}

std::optional<CentralTrackFitResult> fitCentralTrackMeasurements(
    const std::vector<SpacePointMeasurement> &input,
    bool constrainToInteractionPoint, bool refineTransverseCircle) {
  if (input.size() < 4) {
    return std::nullopt;
  }
  auto points = input;
  std::sort(points.begin(), points.end(), [](const auto &left,
                                             const auto &right) {
    return std::hypot(left.position.xMm, left.position.yMm) <
           std::hypot(right.position.xMm, right.position.yMm);
  });

  double sw{}, sx{}, sy{}, sxx{}, syy{}, sxy{}, sb{}, sxb{}, syb{};
  for (const auto &measurement : points) {
    const auto &point = measurement.position;
    if (!std::isfinite(point.xMm) || !std::isfinite(point.yMm) ||
        !std::isfinite(point.zMm) ||
        !std::isfinite(measurement.transverseSigmaMm) ||
        measurement.transverseSigmaMm <= 0 ||
        (measurement.longitudinalSigmaMm &&
         (!std::isfinite(*measurement.longitudinalSigmaMm) ||
          *measurement.longitudinalSigmaMm <= 0))) {
      return std::nullopt;
    }
    const auto weight = 1.0 / std::pow(measurement.transverseSigmaMm, 2);
    const auto b = -(point.xMm * point.xMm + point.yMm * point.yMm);
    sw += weight;
    sx += weight * point.xMm;
    sy += weight * point.yMm;
    sxx += weight * point.xMm * point.xMm;
    syy += weight * point.yMm * point.yMm;
    sxy += weight * point.xMm * point.yMm;
    sb += weight * b;
    sxb += weight * point.xMm * b;
    syb += weight * point.yMm * b;
  }
  double centerX{};
  double centerY{};
  double constant{};
  if (constrainToInteractionPoint) {
    const auto determinant = sxx * syy - sxy * sxy;
    if (std::abs(determinant) < 1e-12) {
      return std::nullopt;
    }
    const auto d = (sxb * syy - syb * sxy) / determinant;
    const auto e = (syb * sxx - sxb * sxy) / determinant;
    centerX = -d / 2.0;
    centerY = -e / 2.0;
  } else {
    const auto circle = solve3x3({{{sxx, sxy, sx, sxb},
                                   {sxy, syy, sy, syb},
                                   {sx, sy, sw, sb}}});
    if (!circle) {
      return std::nullopt;
    }
    centerX = -(*circle)[0] / 2.0;
    centerY = -(*circle)[1] / 2.0;
    constant = (*circle)[2];
  }
  auto radiusSquared = centerX * centerX + centerY * centerY - constant;
  if (!std::isfinite(radiusSquared) || radiusSquared <= 0) {
    return std::nullopt;
  }

  // The algebraic circle is a stable initializer but biases nearly straight
  // high-momentum tracks. For an IP-constrained fit, minimize the actual
  // signed radial residual with deterministic Gauss-Newton steps.
  if (constrainToInteractionPoint && refineTransverseCircle) {
    const auto cost = [&](double candidateX, double candidateY) {
      const auto candidateRadius = std::hypot(candidateX, candidateY);
      double result{};
      for (const auto &measurement : points) {
        const auto &point = measurement.position;
        const auto residual =
            std::hypot(point.xMm - candidateX, point.yMm - candidateY) -
            candidateRadius;
        result += std::pow(residual / measurement.transverseSigmaMm, 2);
      }
      return result;
    };
    for (unsigned int iteration = 0; iteration < 20; ++iteration) {
      const auto radius = std::hypot(centerX, centerY);
      if (radius < 1e-12) {
        return std::nullopt;
      }
      double hxx{}, hxy{}, hyy{}, gx{}, gy{};
      for (const auto &measurement : points) {
        const auto &point = measurement.position;
        const auto dx = centerX - point.xMm;
        const auto dy = centerY - point.yMm;
        const auto distance = std::hypot(dx, dy);
        if (distance < 1e-12) {
          return std::nullopt;
        }
        const auto residual = distance - radius;
        const auto derivativeX = dx / distance - centerX / radius;
        const auto derivativeY = dy / distance - centerY / radius;
        const auto weight =
            1.0 / std::pow(measurement.transverseSigmaMm, 2);
        hxx += weight * derivativeX * derivativeX;
        hxy += weight * derivativeX * derivativeY;
        hyy += weight * derivativeY * derivativeY;
        gx += weight * derivativeX * residual;
        gy += weight * derivativeY * residual;
      }
      const auto determinant = hxx * hyy - hxy * hxy;
      if (std::abs(determinant) < 1e-20) {
        break;
      }
      const auto stepX = -(hyy * gx - hxy * gy) / determinant;
      const auto stepY = -(-hxy * gx + hxx * gy) / determinant;
      const auto oldCost = cost(centerX, centerY);
      auto scale = 1.0;
      while (scale > 1.0 / 1024.0 &&
             cost(centerX + scale * stepX, centerY + scale * stepY) >=
                 oldCost) {
        scale *= 0.5;
      }
      if (scale <= 1.0 / 1024.0) {
        break;
      }
      centerX += scale * stepX;
      centerY += scale * stepY;
      if (std::hypot(scale * stepX, scale * stepY) < 1e-8) {
        break;
      }
    }
    constant = 0.0;
    radiusSquared = centerX * centerX + centerY * centerY;
  }
  const auto radius = std::sqrt(radiusSquared);
  double angularProgress{};
  auto previousAngle = std::atan2(points.front().position.yMm - centerY,
                                  points.front().position.xMm - centerX);
  for (std::size_t index = 1; index < points.size(); ++index) {
    const auto angle = std::atan2(points[index].position.yMm - centerY,
                                  points[index].position.xMm - centerX);
    angularProgress += wrapped(angle - previousAngle);
    previousAngle = angle;
  }
  if (std::abs(angularProgress) < 1e-8) {
    return std::nullopt;
  }
  const auto orientation = angularProgress > 0 ? 1 : -1;

  std::vector<double> arcLengths(points.size());
  const auto firstAngle = std::atan2(points.front().position.yMm - centerY,
                                     points.front().position.xMm - centerX);
  auto unwrappedAngle = firstAngle;
  previousAngle = firstAngle;
  for (std::size_t index = 1; index < points.size(); ++index) {
    const auto angle = std::atan2(points[index].position.yMm - centerY,
                                  points[index].position.xMm - centerX);
    unwrappedAngle += wrapped(angle - previousAngle);
    arcLengths[index] = orientation * radius * (unwrappedAngle - firstAngle);
    previousAngle = angle;
  }
  double longitudinalWeight{}, ss{}, sz{}, sss{}, ssz{};
  std::size_t longitudinalCount{};
  for (std::size_t index = 0; index < points.size(); ++index) {
    if (!points[index].longitudinalSigmaMm) {
      continue;
    }
    const auto weight = 1.0 / std::pow(*points[index].longitudinalSigmaMm, 2);
    longitudinalWeight += weight;
    ss += weight * arcLengths[index];
    sz += weight * points[index].position.zMm;
    sss += weight * arcLengths[index] * arcLengths[index];
    ssz += weight * arcLengths[index] * points[index].position.zMm;
    ++longitudinalCount;
  }
  const auto denominator = longitudinalWeight * sss - ss * ss;
  if (longitudinalCount < 2 || std::abs(denominator) < 1e-12) {
    return std::nullopt;
  }
  const auto tanLambda =
      (longitudinalWeight * ssz - ss * sz) / denominator;
  const auto zAtFirst = (sz - tanLambda * ss) / longitudinalWeight;

  const auto centerDistance = std::hypot(centerX, centerY);
  if (centerDistance < 1e-12) {
    return std::nullopt;
  }
  const auto perigeeX = constrainToInteractionPoint
                            ? 0.0
                            : centerX * (1.0 - radius / centerDistance);
  const auto perigeeY = constrainToInteractionPoint
                            ? 0.0
                            : centerY * (1.0 - radius / centerDistance);
  const auto radialX = (perigeeX - centerX) / radius;
  const auto radialY = (perigeeY - centerY) / radius;
  const auto tangentX = orientation * -radialY;
  const auto tangentY = orientation * radialX;
  const auto phi = std::atan2(tangentY, tangentX);
  const auto d0 = -perigeeX * std::sin(phi) + perigeeY * std::cos(phi);
  const auto perigeeAngle = std::atan2(perigeeY - centerY,
                                       perigeeX - centerX);
  auto firstFromPerigee = orientation * wrapped(firstAngle - perigeeAngle);
  if (firstFromPerigee < 0) {
    firstFromPerigee += 2.0 * std::numbers::pi;
  }
  const auto z0 = zAtFirst - tanLambda * radius * firstFromPerigee;

  double chi2{};
  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto &point = points[index].position;
    const auto radialResidual =
        std::hypot(point.xMm - centerX, point.yMm - centerY) -
        radius;
    chi2 += std::pow(radialResidual / points[index].transverseSigmaMm, 2);
    if (points[index].longitudinalSigmaMm) {
      const auto zResidual =
          point.zMm - (zAtFirst + tanLambda * arcLengths[index]);
      chi2 += std::pow(zResidual / *points[index].longitudinalSigmaMm, 2);
    }
  }
  return CentralTrackFitResult{d0,
                               phi,
                               -static_cast<double>(orientation) / radius,
                               z0,
                               tanLambda,
                               chi2,
                               static_cast<int>(points.size() +
                                                longitudinalCount) -
                                   5,
                               centerX,
                               centerY,
                               radius,
                               orientation};
}

} // namespace delphi_edm4hep::reconstruction
