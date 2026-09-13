#include "delphi_edm4hep/Reconstruction/HelixTrajectory.h"

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
#include "edm4hep/MutableTrack.h"
#include "edm4hep/TrackCollection.h"
#include "edm4hep/TrackState.h"
#include "edm4hep/TrackerHitPlaneCollection.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace delphi_edm4hep {
namespace {

constexpr std::uint64_t idDriftSideBit = std::uint64_t{1} << 39U;
constexpr std::uint64_t odDriftSideBit = std::uint64_t{1} << 31U;

void cloneTrack(edm4hep::MutableTrack destination,
                const edm4hep::Track &source) {
  destination.setType(source.getType());
  destination.setChi2(source.getChi2());
  destination.setNdf(source.getNdf());
  destination.setNholes(source.getNholes());
  for (const auto number : source.getSubdetectorHitNumbers()) {
    destination.addToSubdetectorHitNumbers(number);
  }
  for (const auto number : source.getSubdetectorHoleNumbers()) {
    destination.addToSubdetectorHoleNumbers(number);
  }
  for (const auto &state : source.getTrackStates()) {
    destination.addToTrackStates(state);
  }
  for (const auto &hit : source.getTrackerHits()) {
    destination.addToTrackerHits(hit);
  }
  for (const auto &segment : source.getTracks()) {
    destination.addToTracks(segment);
  }
}

std::optional<reconstruction::HelixTrajectory>
trajectory(const edm4hep::Track &track) {
  for (const auto &state : track.getTrackStates()) {
    if (state.location != edm4hep::TrackState::AtIP) {
      continue;
    }
    reconstruction::HelixTrajectory result(
        state.D0, state.phi, state.omega, state.Z0, state.tanLambda);
    if (result.valid()) {
      return result;
    }
  }
  return std::nullopt;
}

struct Match {
  std::size_t track{};
  std::size_t hit{};
  double score{std::numeric_limits<double>::infinity()};
};

} // namespace

class DelphiCentralTrackExtensionProducer final
    : public edm::global::EDProducer<> {
public:
  explicit DelphiCentralTrackExtensionProducer(
      const edm::ParameterSet &config)
      : trackToken_(consumes(config.getParameter<edm::InputTag>("tracks"))),
        vertexHitToken_(
            consumes(config.getParameter<edm::InputTag>("vertexHits"))),
        innerDetectorHitToken_(consumes(
            config.getParameter<edm::InputTag>("innerDetectorHits"))),
        outerDetectorHitToken_(consumes(
            config.getParameter<edm::InputTag>("outerDetectorHits"))),
        outputToken_(
            produces<edm4hep::TrackCollection>("ExtendedCentralTracks")),
        vertexTransverseWindowMm_(
            config.getParameter<double>("vertexTransverseWindowMm")),
        vertexLongitudinalWindowMm_(
            config.getParameter<double>("vertexLongitudinalWindowMm")),
        innerDetectorTransverseWindowMm_(config.getParameter<double>(
            "innerDetectorTransverseWindowMm")),
        outerDetectorTransverseWindowMm_(config.getParameter<double>(
            "outerDetectorTransverseWindowMm")),
        outerDetectorLongitudinalWindowMm_(config.getParameter<double>(
            "outerDetectorLongitudinalWindowMm")) {
    if (vertexTransverseWindowMm_ <= 0.0 ||
        vertexLongitudinalWindowMm_ <= 0.0 ||
        innerDetectorTransverseWindowMm_ <= 0.0 ||
        outerDetectorTransverseWindowMm_ <= 0.0 ||
        outerDetectorLongitudinalWindowMm_ <= 0.0) {
      throw std::invalid_argument(
          "central track extension windows must all be positive");
    }
  }

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("tracks");
    description.add<edm::InputTag>("vertexHits");
    description.add<edm::InputTag>("innerDetectorHits");
    description.add<edm::InputTag>("outerDetectorHits");
    description.add<double>("vertexTransverseWindowMm", 5.0);
    description.add<double>("vertexLongitudinalWindowMm", 20.0);
    description.add<double>("innerDetectorTransverseWindowMm", 20.0);
    description.add<double>("outerDetectorTransverseWindowMm", 50.0);
    description.add<double>("outerDetectorLongitudinalWindowMm", 150.0);
    descriptions.addDefault(description);
  }

private:
  void produce(edm::StreamID, edm::Event &event,
               const edm::EventSetup &) const final {
    const auto &tracks = event.get(trackToken_);
    const auto &vertexHits = event.get(vertexHitToken_);
    const auto &innerDetectorHits = event.get(innerDetectorHitToken_);
    const auto &outerDetectorHits = event.get(outerDetectorHitToken_);

    edm4hep::TrackCollection output;
    std::vector<edm4hep::MutableTrack> outputTracks;
    std::vector<std::optional<reconstruction::HelixTrajectory>> trajectories;
    outputTracks.reserve(tracks.size());
    trajectories.reserve(tracks.size());
    for (const auto track : tracks) {
      auto extended = output.create();
      cloneTrack(extended, track);
      outputTracks.push_back(extended);
      trajectories.push_back(trajectory(track));
    }

    // Silicon hits have no drift ambiguity. Give each hit to at most one
    // candidate, choosing the closest compatible helix in both coordinates.
    for (std::size_t hitIndex = 0; hitIndex < vertexHits.size(); ++hitIndex) {
      const auto position = vertexHits[hitIndex].getPosition();
      Match best;
      for (std::size_t trackIndex = 0; trackIndex < trajectories.size();
           ++trackIndex) {
        if (!trajectories[trackIndex]) {
          continue;
        }
        const auto residual =
            trajectories[trackIndex]->at(position[0], position[1], position[2]);
        if (std::abs(residual.transverseResidualMm) >
                vertexTransverseWindowMm_ ||
            std::abs(residual.longitudinalResidualMm) >
                vertexLongitudinalWindowMm_) {
          continue;
        }
        const auto score = std::hypot(
            residual.transverseResidualMm / vertexTransverseWindowMm_,
            residual.longitudinalResidualMm / vertexLongitudinalWindowMm_);
        if (score < best.score) {
          best = {trackIndex, hitIndex, score};
        }
      }
      if (std::isfinite(best.score)) {
        outputTracks[best.track].addToTrackerHits(vertexHits[best.hit]);
      }
    }

    // ID and OD reconstruction deliberately emits both drift sides. Resolve
    // each physical channel globally: only the best helix/hypothesis pair is
    // retained, so a hit cannot silently be shared by two track candidates.
    associateDriftHits(innerDetectorHits, trajectories, outputTracks,
                       idDriftSideBit, innerDetectorTransverseWindowMm_,
                       std::nullopt);
    associateDriftHits(outerDetectorHits, trajectories, outputTracks,
                       odDriftSideBit, outerDetectorTransverseWindowMm_,
                       outerDetectorLongitudinalWindowMm_);

    c4h::setCollectionID(output, event, *this, outputToken_);
    event.emplace(outputToken_, std::move(output));
  }

  static void associateDriftHits(
      const edm4hep::TrackerHitPlaneCollection &hits,
      const std::vector<std::optional<reconstruction::HelixTrajectory>>
          &trajectories,
      std::vector<edm4hep::MutableTrack> &outputTracks,
      std::uint64_t sideBit, double transverseWindowMm,
      std::optional<double> longitudinalWindowMm) {
    std::map<std::uint64_t, std::vector<std::size_t>> hypotheses;
    for (std::size_t index = 0; index < hits.size(); ++index) {
      hypotheses[hits[index].getCellID() & ~sideBit].push_back(index);
    }
    for (const auto &[channel, channelHits] : hypotheses) {
      static_cast<void>(channel);
      Match best;
      for (const auto hitIndex : channelHits) {
        const auto position = hits[hitIndex].getPosition();
        for (std::size_t trackIndex = 0; trackIndex < trajectories.size();
             ++trackIndex) {
          if (!trajectories[trackIndex]) {
            continue;
          }
          const auto residual = trajectories[trackIndex]->at(
              position[0], position[1], position[2]);
          if (std::abs(residual.transverseResidualMm) > transverseWindowMm ||
              (longitudinalWindowMm &&
               std::abs(residual.longitudinalResidualMm) >
                   *longitudinalWindowMm)) {
            continue;
          }
          auto score =
              std::abs(residual.transverseResidualMm) / transverseWindowMm;
          if (longitudinalWindowMm) {
            score = std::hypot(score, std::abs(residual.longitudinalResidualMm) /
                                          *longitudinalWindowMm);
          }
          if (score < best.score) {
            best = {trackIndex, hitIndex, score};
          }
        }
      }
      if (std::isfinite(best.score)) {
        outputTracks[best.track].addToTrackerHits(hits[best.hit]);
      }
    }
  }

  const edm::EDGetTokenT<edm4hep::TrackCollection> trackToken_;
  const edm::EDGetTokenT<edm4hep::TrackerHitPlaneCollection> vertexHitToken_;
  const edm::EDGetTokenT<edm4hep::TrackerHitPlaneCollection>
      innerDetectorHitToken_;
  const edm::EDGetTokenT<edm4hep::TrackerHitPlaneCollection>
      outerDetectorHitToken_;
  const edm::EDPutTokenT<edm4hep::TrackCollection> outputToken_;
  const double vertexTransverseWindowMm_;
  const double vertexLongitudinalWindowMm_;
  const double innerDetectorTransverseWindowMm_;
  const double outerDetectorTransverseWindowMm_;
  const double outerDetectorLongitudinalWindowMm_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiCentralTrackExtensionProducer);
