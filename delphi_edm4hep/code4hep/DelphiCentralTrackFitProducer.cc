#include "delphi_edm4hep/Reconstruction/CentralTrackFinder.h"
#include "delphi_edm4hep/Simulation/TpcReadoutGeometry.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/EDGetToken.h"
#include "FWCore/Utilities/interface/EDPutToken.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"
#include "edm4hep/TrackCollection.h"
#include "edm4hep/TrackState.h"
#include "edm4hep/TrackerHit3DCollection.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace delphi_edm4hep {

class DelphiCentralTrackFitProducer final : public edm::global::EDProducer<> {
public:
  explicit DelphiCentralTrackFitProducer(const edm::ParameterSet &config)
      : inputToken_(consumes(config.getParameter<edm::InputTag>("tpcHits"))),
        outputToken_(produces<edm4hep::TrackCollection>("CentralTracks")),
        finderConfig_{
            config.getParameter<unsigned int>("minimumRows"),
            config.getParameter<double>("transverseSigmaMm"),
            config.getParameter<double>("longitudinalSigmaMm"),
            config.getParameter<double>("transverseResidualWindowMm"),
            config.getParameter<double>("longitudinalResidualWindowMm"),
            config.getParameter<bool>("constrainToInteractionPoint")} {}

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("tpcHits");
    description.add<unsigned int>("minimumRows", 8U);
    description.add<double>("transverseSigmaMm", 5.0);
    description.add<double>("longitudinalSigmaMm", 10.0);
    description.add<double>("transverseResidualWindowMm", 15.0);
    description.add<double>("longitudinalResidualWindowMm", 50.0);
    description.add<bool>("constrainToInteractionPoint", true);
    descriptions.addDefault(description);
  }

private:
  void produce(edm::StreamID, edm::Event &event,
               const edm::EventSetup &) const final {
    using RowKey = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>;
    std::map<RowKey, std::vector<std::pair<std::uint32_t, std::size_t>>> rows;
    const auto &hits = event.get(inputToken_);
    for (std::size_t index = 0; index < hits.size(); ++index) {
      const auto hit = hits[index];
      const auto address =
          simulation::TpcReadoutGeometry::decodeCellId(hit.getCellID());
      rows[{address.endcap, address.sector, address.row}].push_back(
          {address.pad, index});
    }

    std::vector<reconstruction::CentralTrackCluster> clusters;
    for (auto &[key, rowHits] : rows) {
      std::sort(rowHits.begin(), rowHits.end());
      std::size_t begin{};
      while (begin < rowHits.size()) {
        auto end = begin + 1;
        while (end < rowHits.size() &&
               rowHits[end].first <= rowHits[end - 1].first + 1U) {
          ++end;
        }
        reconstruction::CentralTrackCluster cluster;
        cluster.endcap = std::get<0>(key);
        cluster.sector = std::get<1>(key);
        cluster.row = std::get<2>(key);
        for (auto index = begin; index < end; ++index) {
          const auto hitIndex = rowHits[index].second;
          const auto position = hits[hitIndex].getPosition();
          cluster.position.xMm += position[0];
          cluster.position.yMm += position[1];
          cluster.position.zMm += position[2];
          cluster.hitIndices.push_back(hitIndex);
        }
        const auto count = static_cast<double>(cluster.hitIndices.size());
        cluster.position.xMm /= count;
        cluster.position.yMm /= count;
        cluster.position.zMm /= count;
        clusters.push_back(std::move(cluster));
        begin = end;
      }
    }

    edm4hep::TrackCollection output;
    for (const auto &candidate :
         reconstruction::findCentralTracks(clusters, finderConfig_)) {
      const auto &fit = candidate.fit;
      edm4hep::TrackState state{};
      state.location = edm4hep::TrackState::AtIP;
      state.D0 = static_cast<float>(fit.d0Mm);
      state.phi = static_cast<float>(fit.phiRadians);
      state.omega = static_cast<float>(fit.omegaPerMm);
      state.Z0 = static_cast<float>(fit.z0Mm);
      state.tanLambda = static_cast<float>(fit.tanLambda);
      state.time = 0.0F;
      state.referencePoint = {0.0F, 0.0F, 0.0F};
      using P = edm4hep::TrackParams;
      double minimumRadius = std::numeric_limits<double>::infinity();
      double maximumRadius{};
      for (const auto clusterIndex : candidate.clusterIndices) {
        const auto &point = clusters[clusterIndex].position;
        const auto radius = std::hypot(point.xMm, point.yMm);
        minimumRadius = std::min(minimumRadius, radius);
        maximumRadius = std::max(maximumRadius, radius);
      }
      const auto radialSpan = std::max(1.0, maximumRadius - minimumRadius);
      state.covMatrix.setValue(finderConfig_.transverseSigmaMm *
                                   finderConfig_.transverseSigmaMm,
                               P::d0, P::d0);
      state.covMatrix.setValue(
          std::pow(finderConfig_.transverseSigmaMm / radialSpan, 2), P::phi,
          P::phi);
      state.covMatrix.setValue(
          std::pow(finderConfig_.transverseSigmaMm / (radialSpan * radialSpan),
                   2),
          P::omega, P::omega);
      state.covMatrix.setValue(finderConfig_.longitudinalSigmaMm *
                                   finderConfig_.longitudinalSigmaMm,
                               P::z0, P::z0);
      state.covMatrix.setValue(
          std::pow(finderConfig_.longitudinalSigmaMm / radialSpan, 2),
          P::tanLambda, P::tanLambda);

      auto track = output.create();
      track.setType(finderConfig_.constrainToInteractionPoint ? 1 : 0);
      track.setChi2(static_cast<float>(fit.chi2));
      track.setNdf(fit.ndf);
      track.setNholes(static_cast<int>(
          16 - std::min<std::size_t>(16, candidate.clusterIndices.size())));
      track.addToTrackStates(state);
      for (const auto hitIndex : candidate.hitIndices) {
        track.addToTrackerHits(hits.at(hitIndex));
      }
    }
    c4h::setCollectionID(output, event, *this, outputToken_);
    event.emplace(outputToken_, std::move(output));
  }

  const edm::EDGetTokenT<edm4hep::TrackerHit3DCollection> inputToken_;
  const edm::EDPutTokenT<edm4hep::TrackCollection> outputToken_;
  const reconstruction::CentralTrackFinderConfig finderConfig_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiCentralTrackFitProducer);
