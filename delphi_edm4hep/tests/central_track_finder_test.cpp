#include "delphi_edm4hep/Reconstruction/CentralTrackFinder.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>

namespace {

delphi_edm4hep::reconstruction::SpacePoint
point(double radiusMm, double phi, double arcAngle, double tanLambda) {
  const auto localX = radiusMm * std::sin(arcAngle);
  const auto localY = radiusMm * (1.0 - std::cos(arcAngle));
  return {std::cos(phi) * localX - std::sin(phi) * localY,
          std::sin(phi) * localX + std::cos(phi) * localY,
          tanLambda * radiusMm * arcAngle};
}

} // namespace

int main() {
  using namespace delphi_edm4hep::reconstruction;
  std::vector<CentralTrackCluster> clusters;
  for (std::uint32_t row = 1; row <= 16; ++row) {
    clusters.push_back(
        {point(10000.0, 0.15, 0.003 * row, 0.30), 1, row, {2 * row}});
    clusters.back().sector = row <= 8 ? 2 : 3;
    clusters.push_back(
        {point(7000.0, 0.85, 0.004 * row, 0.55), 1, row, {2 * row + 1}});
    clusters.back().sector = row <= 8 ? 5 : 6;
  }
  // Sparse unrelated clusters must not become a track.
  for (std::uint32_t row = 1; row <= 6; ++row) {
    clusters.push_back({{350.0 + 70.0 * row, -600.0 + 31.0 * row, 20.0 * row},
                        1,
                        row,
                        {100 + row}});
  }

  CentralTrackFinderConfig config;
  config.minimumRows = 12;
  config.transverseSigmaMm = 0.1;
  config.longitudinalSigmaMm = 0.1;
  config.transverseResidualWindowMm = 1.0;
  config.longitudinalResidualWindowMm = 1.0;
  const auto tracks = findCentralTracks(clusters, config);
  if (tracks.size() != 2) {
    throw std::runtime_error(
        "native pattern recognition lost two-track closure");
  }
  std::vector<double> curvatures;
  std::set<std::size_t> claimed;
  for (const auto &track : tracks) {
    if (track.clusterIndices.size() != 16 || track.hitIndices.size() != 16) {
      throw std::runtime_error("native track has incomplete row support");
    }
    for (const auto index : track.clusterIndices) {
      if (!claimed.insert(index).second) {
        throw std::runtime_error("TPC cluster was assigned to two tracks");
      }
    }
    std::set<std::uint32_t> sectors;
    for (const auto index : track.clusterIndices) {
      sectors.insert(clusters[index].sector);
    }
    if (sectors.size() != 2) {
      throw std::runtime_error("native track did not cross a sector boundary");
    }
    curvatures.push_back(track.fit.omegaPerMm);
  }
  std::sort(curvatures.begin(), curvatures.end());
  const std::vector<double> expected{-1.0 / 7000.0, -1.0 / 10000.0};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (std::abs(curvatures[index] - expected[index]) > 1e-10) {
      throw std::runtime_error("native pattern-recognition curvature changed");
    }
  }
}
