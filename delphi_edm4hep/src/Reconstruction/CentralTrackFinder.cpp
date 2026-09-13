#include "delphi_edm4hep/Reconstruction/CentralTrackFinder.h"

#include "delphi_edm4hep/Reconstruction/HelixTrajectory.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace delphi_edm4hep::reconstruction {
namespace {

struct Circle {
  double centerX{};
  double centerY{};
  double radius{};
};

std::optional<Circle> circleThroughOrigin(const SpacePoint &first,
                                          const SpacePoint &second) {
  const auto determinant =
      2.0 * (first.xMm * second.yMm - first.yMm * second.xMm);
  if (std::abs(determinant) < 1e-9) {
    return std::nullopt;
  }
  const auto firstRadiusSquared = first.xMm * first.xMm + first.yMm * first.yMm;
  const auto secondRadiusSquared =
      second.xMm * second.xMm + second.yMm * second.yMm;
  const auto centerX =
      (firstRadiusSquared * second.yMm - secondRadiusSquared * first.yMm) /
      determinant;
  const auto centerY =
      (first.xMm * secondRadiusSquared - second.xMm * firstRadiusSquared) /
      determinant;
  const auto radius = std::hypot(centerX, centerY);
  if (!std::isfinite(radius) || radius < 100.0 || radius > 1.0e8) {
    return std::nullopt;
  }
  return Circle{centerX, centerY, radius};
}

double transverseResidual(const Circle &circle, const SpacePoint &point) {
  return std::abs(
      std::hypot(point.xMm - circle.centerX, point.yMm - circle.centerY) -
      circle.radius);
}

struct Hypothesis {
  std::vector<std::size_t> clusters;
  double residual{std::numeric_limits<double>::infinity()};
  std::size_t firstSeed{std::numeric_limits<std::size_t>::max()};
  std::size_t secondSeed{std::numeric_limits<std::size_t>::max()};
};

bool better(const Hypothesis &candidate, const Hypothesis &current) {
  if (candidate.clusters.size() != current.clusters.size()) {
    return candidate.clusters.size() > current.clusters.size();
  }
  if (std::abs(candidate.residual - current.residual) > 1e-12) {
    return candidate.residual < current.residual;
  }
  return std::pair{candidate.firstSeed, candidate.secondSeed} <
         std::pair{current.firstSeed, current.secondSeed};
}

Hypothesis selectTransverse(const Circle &circle, std::uint32_t endcap,
                            const std::vector<CentralTrackCluster> &clusters,
                            const std::vector<bool> &available, double windowMm,
                            std::size_t firstSeed, std::size_t secondSeed) {
  std::map<std::uint32_t, std::pair<std::size_t, double>> bestByRow;
  for (std::size_t index = 0; index < clusters.size(); ++index) {
    if (!available[index] || clusters[index].endcap != endcap) {
      continue;
    }
    const auto residual = transverseResidual(circle, clusters[index].position);
    if (residual > windowMm) {
      continue;
    }
    const auto found = bestByRow.find(clusters[index].row);
    if (found == bestByRow.end() || residual < found->second.second ||
        (residual == found->second.second && index < found->second.first)) {
      bestByRow[clusters[index].row] = {index, residual};
    }
  }
  Hypothesis result;
  result.firstSeed = firstSeed;
  result.secondSeed = secondSeed;
  result.residual = 0.0;
  for (const auto &[row, selected] : bestByRow) {
    static_cast<void>(row);
    result.clusters.push_back(selected.first);
    result.residual += selected.second * selected.second;
  }
  return result;
}

std::vector<std::size_t>
selectHelix(const HelixTrajectory &trajectory, std::uint32_t endcap,
            const std::vector<CentralTrackCluster> &clusters,
            const std::vector<bool> &available,
            const CentralTrackFinderConfig &config) {
  std::map<std::uint32_t, std::pair<std::size_t, double>> bestByRow;
  for (std::size_t index = 0; index < clusters.size(); ++index) {
    if (!available[index] || clusters[index].endcap != endcap) {
      continue;
    }
    const auto &point = clusters[index].position;
    const auto residual = trajectory.at(point.xMm, point.yMm, point.zMm);
    if (std::abs(residual.transverseResidualMm) >
            config.transverseResidualWindowMm ||
        std::abs(residual.longitudinalResidualMm) >
            config.longitudinalResidualWindowMm) {
      continue;
    }
    const auto score = std::hypot(
        residual.transverseResidualMm / config.transverseResidualWindowMm,
        residual.longitudinalResidualMm / config.longitudinalResidualWindowMm);
    const auto found = bestByRow.find(clusters[index].row);
    if (found == bestByRow.end() || score < found->second.second ||
        (score == found->second.second && index < found->second.first)) {
      bestByRow[clusters[index].row] = {index, score};
    }
  }
  std::vector<std::size_t> result;
  result.reserve(bestByRow.size());
  for (const auto &[row, selected] : bestByRow) {
    static_cast<void>(row);
    result.push_back(selected.first);
  }
  return result;
}

std::optional<CentralTrackFitResult>
fitClusters(const std::vector<std::size_t> &indices,
            const std::vector<CentralTrackCluster> &clusters,
            const CentralTrackFinderConfig &config) {
  std::vector<SpacePoint> points;
  points.reserve(indices.size());
  for (const auto index : indices) {
    points.push_back(clusters[index].position);
  }
  return fitCentralTrack(points, config.transverseSigmaMm,
                         config.longitudinalSigmaMm,
                         config.constrainToInteractionPoint);
}

} // namespace

std::vector<CentralTrackCandidate>
findCentralTracks(const std::vector<CentralTrackCluster> &clusters,
                  const CentralTrackFinderConfig &config) {
  if (config.minimumRows < 4 || config.transverseSigmaMm <= 0.0 ||
      config.longitudinalSigmaMm <= 0.0 ||
      config.transverseResidualWindowMm <= 0.0 ||
      config.longitudinalResidualWindowMm <= 0.0) {
    throw std::invalid_argument("invalid central track finder configuration");
  }
  std::vector<bool> available(clusters.size(), true);
  std::vector<CentralTrackCandidate> result;
  while (true) {
    Hypothesis best;
    for (std::size_t first = 0; first < clusters.size(); ++first) {
      if (!available[first]) {
        continue;
      }
      for (std::size_t second = first + 1; second < clusters.size(); ++second) {
        if (!available[second] ||
            clusters[first].endcap != clusters[second].endcap ||
            clusters[first].row == clusters[second].row ||
            std::abs(std::hypot(clusters[first].position.xMm,
                                clusters[first].position.yMm) -
                     std::hypot(clusters[second].position.xMm,
                                clusters[second].position.yMm)) < 100.0) {
          continue;
        }
        const auto circle = circleThroughOrigin(clusters[first].position,
                                                clusters[second].position);
        if (!circle) {
          continue;
        }
        auto candidate = selectTransverse(
            *circle, clusters[first].endcap, clusters, available,
            config.transverseResidualWindowMm, first, second);
        if (better(candidate, best)) {
          best = std::move(candidate);
        }
      }
    }
    if (best.clusters.size() < config.minimumRows) {
      break;
    }
    auto fit = fitClusters(best.clusters, clusters, config);
    if (!fit) {
      break;
    }
    HelixTrajectory trajectory(fit->d0Mm, fit->phiRadians, fit->omegaPerMm,
                               fit->z0Mm, fit->tanLambda);
    if (!trajectory.valid()) {
      break;
    }
    auto selected =
        selectHelix(trajectory, clusters[best.clusters.front()].endcap,
                    clusters, available, config);
    if (selected.size() < config.minimumRows) {
      break;
    }
    fit = fitClusters(selected, clusters, config);
    if (!fit) {
      break;
    }

    CentralTrackCandidate candidate{*fit, selected, {}};
    for (const auto clusterIndex : selected) {
      available[clusterIndex] = false;
      candidate.hitIndices.insert(candidate.hitIndices.end(),
                                  clusters[clusterIndex].hitIndices.begin(),
                                  clusters[clusterIndex].hitIndices.end());
    }
    result.push_back(std::move(candidate));
  }
  return result;
}

} // namespace delphi_edm4hep::reconstruction
