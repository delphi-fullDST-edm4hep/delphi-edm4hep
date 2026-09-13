#pragma once

#include "delphi_edm4hep/Reconstruction/CentralTrackFit.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace delphi_edm4hep::reconstruction {

struct CentralTrackCluster {
  SpacePoint position;
  std::uint32_t endcap{};
  std::uint32_t row{};
  std::vector<std::size_t> hitIndices;
  std::uint32_t sector{};
};

struct CentralTrackFinderConfig {
  std::size_t minimumRows{8};
  double transverseSigmaMm{5.0};
  double longitudinalSigmaMm{10.0};
  double transverseResidualWindowMm{15.0};
  double longitudinalResidualWindowMm{50.0};
  bool constrainToInteractionPoint{true};
};

struct CentralTrackCandidate {
  CentralTrackFitResult fit;
  std::vector<std::size_t> clusterIndices;
  std::vector<std::size_t> hitIndices;
};

std::vector<CentralTrackCandidate>
findCentralTracks(const std::vector<CentralTrackCluster> &clusters,
                  const CentralTrackFinderConfig &config = {});

} // namespace delphi_edm4hep::reconstruction
