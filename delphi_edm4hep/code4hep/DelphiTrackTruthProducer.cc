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
#include "edm4hep/TrackMCParticleLinkCollection.h"
#include "edm4hep/TrackerHitSimTrackerHitLinkCollection.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace delphi_edm4hep {
namespace {

using ObjectKey = std::pair<unsigned int, int>;

template <typename Handle> ObjectKey objectKey(const Handle &handle) {
  const auto id = handle.getObjectID();
  return {id.collectionID, id.index};
}

struct TruthContribution {
  edm4hep::MCParticle particle;
  double weight{};
};

} // namespace

class DelphiTrackTruthProducer final : public edm::global::EDProducer<> {
public:
  explicit DelphiTrackTruthProducer(const edm::ParameterSet &config)
      : trackToken_(consumes(config.getParameter<edm::InputTag>("tracks"))),
        vertexTruthToken_(
            consumes(config.getParameter<edm::InputTag>("vertexHitTruth"))),
        innerDetectorTruthToken_(consumes(
            config.getParameter<edm::InputTag>("innerDetectorHitTruth"))),
        tpcTruthToken_(
            consumes(config.getParameter<edm::InputTag>("tpcHitTruth"))),
        outerDetectorTruthToken_(consumes(
            config.getParameter<edm::InputTag>("outerDetectorHitTruth"))),
        outputToken_(produces<edm4hep::TrackMCParticleLinkCollection>(
            "TrackMCParticleLinks")),
        minimumWeight_(config.getParameter<double>("minimumWeight")) {
    if (!std::isfinite(minimumWeight_) || minimumWeight_ < 0.0 ||
        minimumWeight_ > 1.0) {
      throw std::invalid_argument(
          "track-truth minimumWeight must be in the interval [0, 1]");
    }
  }

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("tracks");
    description.add<edm::InputTag>("vertexHitTruth");
    description.add<edm::InputTag>("innerDetectorHitTruth");
    description.add<edm::InputTag>("tpcHitTruth");
    description.add<edm::InputTag>("outerDetectorHitTruth");
    description.add<double>("minimumWeight", 0.0);
    descriptions.addDefault(description);
  }

private:
  using HitTruthMap = std::map<ObjectKey, std::vector<TruthContribution>>;

  static void addTruth(
      const edm4hep::TrackerHitSimTrackerHitLinkCollection &links,
      HitTruthMap &truthByHit) {
    for (const auto link : links) {
      const auto hit = link.getFrom();
      const auto simHit = link.getTo();
      if (!hit.isAvailable() || !simHit.isAvailable() ||
          !simHit.getParticle().isAvailable()) {
        throw std::runtime_error(
            "track truth input contains an unresolved relation");
      }
      truthByHit[objectKey(hit)].push_back(
          {simHit.getParticle(), link.getWeight()});
    }
  }

  void produce(edm::StreamID, edm::Event &event,
               const edm::EventSetup &) const final {
    HitTruthMap truthByHit;
    addTruth(event.get(vertexTruthToken_), truthByHit);
    addTruth(event.get(innerDetectorTruthToken_), truthByHit);
    addTruth(event.get(tpcTruthToken_), truthByHit);
    addTruth(event.get(outerDetectorTruthToken_), truthByHit);

    edm4hep::TrackMCParticleLinkCollection output;
    for (const auto track : event.get(trackToken_)) {
      std::map<ObjectKey, TruthContribution> contributions;
      for (const auto &hit : track.getTrackerHits()) {
        const auto found = truthByHit.find(objectKey(hit));
        if (found == truthByHit.end()) {
          continue;
        }
        for (const auto &contribution : found->second) {
          const auto key = objectKey(contribution.particle);
          auto [entry, inserted] = contributions.try_emplace(
              key, TruthContribution{contribution.particle, 0.0});
          static_cast<void>(inserted);
          entry->second.weight += contribution.weight;
        }
      }
      double totalWeight{};
      for (const auto &[key, contribution] : contributions) {
        static_cast<void>(key);
        totalWeight += contribution.weight;
      }
      if (totalWeight <= 0.0) {
        continue;
      }
      for (const auto &[key, contribution] : contributions) {
        static_cast<void>(key);
        const auto normalizedWeight = contribution.weight / totalWeight;
        if (normalizedWeight < minimumWeight_) {
          continue;
        }
        auto link = output.create();
        link.setFrom(track);
        link.setTo(contribution.particle);
        link.setWeight(static_cast<float>(normalizedWeight));
      }
    }
    c4h::setCollectionID(output, event, *this, outputToken_);
    event.emplace(outputToken_, std::move(output));
  }

  const edm::EDGetTokenT<edm4hep::TrackCollection> trackToken_;
  const edm::EDGetTokenT<edm4hep::TrackerHitSimTrackerHitLinkCollection>
      vertexTruthToken_;
  const edm::EDGetTokenT<edm4hep::TrackerHitSimTrackerHitLinkCollection>
      innerDetectorTruthToken_;
  const edm::EDGetTokenT<edm4hep::TrackerHitSimTrackerHitLinkCollection>
      tpcTruthToken_;
  const edm::EDGetTokenT<edm4hep::TrackerHitSimTrackerHitLinkCollection>
      outerDetectorTruthToken_;
  const edm::EDPutTokenT<edm4hep::TrackMCParticleLinkCollection> outputToken_;
  const double minimumWeight_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiTrackTruthProducer);
