#include "delphi_edm4hep/Code4hep/RawTimeSeriesSimTrackerHitLinkCollection.h"
#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Simulation/OuterDetectorDriftResponse.h"
#include "delphi_edm4hep/Simulation/OuterDetectorPhysicalChannelPayload.h"
#include "delphi_edm4hep/Simulation/OuterDetectorReadoutGeometry.h"

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
#include "edm4hep/RawTimeSeriesCollection.h"
#include "edm4hep/TrackerHitPlaneCollection.h"
#include "edm4hep/TrackerHitSimTrackerHitLinkCollection.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace delphi_edm4hep {

class DelphiOuterDetectorHitReconstructionProducer final
    : public edm::global::EDProducer<> {
public:
  explicit DelphiOuterDetectorHitReconstructionProducer(
      const edm::ParameterSet &config)
      : inputToken_(consumes(config.getParameter<edm::InputTag>("digis"))),
        truthInputToken_(
            consumes(config.getParameter<edm::InputTag>("digiTruthLinks"))),
        outputToken_(produces<edm4hep::TrackerHitPlaneCollection>(
            "OuterDetectorHits")),
        truthOutputToken_(
            produces<edm4hep::TrackerHitSimTrackerHitLinkCollection>(
                "OuterDetectorHitSimTrackerHitLinks")),
        readout_(simulation::OuterDetectorReadoutGeometry::fromCargo(
            geometry::CargoDatabase::readFile(
                config.getParameter<std::string>("cargoSnapshot")))),
        response_{} {}

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("digis");
    description.add<edm::InputTag>("digiTruthLinks");
    description.add<std::string>("cargoSnapshot");
    descriptions.addDefault(description);
  }

private:
  void produce(edm::StreamID, edm::Event &event,
               const edm::EventSetup &) const final {
    struct TruthContribution {
      edm4hep::SimTrackerHit hit;
      float weight{};
    };
    std::unordered_map<int, std::vector<TruthContribution>> truthByDigi;
    for (const auto link : event.get(truthInputToken_)) {
      if (!link.getFrom().isAvailable() || !link.getTo().isAvailable()) {
        throw std::runtime_error("OD digit truth link is unresolved");
      }
      truthByDigi[link.getFrom().getObjectID().index].push_back(
          {link.getTo(), link.getWeight()});
    }

    edm4hep::TrackerHitPlaneCollection output;
    edm4hep::TrackerHitSimTrackerHitLinkCollection truthOutput;
    for (const auto digi : event.get(inputToken_)) {
      if (digi.getQuality() !=
              simulation::outerDetectorPhysicalChannelPayloadVersion ||
          digi.adcCounts_size() != 4) {
        throw std::runtime_error("unsupported OD physical-channel payload");
      }
      const auto channel =
          simulation::OuterDetectorReadoutGeometry::decodeChannelID(
              digi.getCellID());
      const auto payload =
          simulation::decodeOuterDetectorPhysicalChannelPayload(
              {digi.getAdcCounts(0), digi.getAdcCounts(1),
               digi.getAdcCounts(2), digi.getAdcCounts(3)});
      const auto maximumDistance =
          0.5 * std::hypot(readout_.cellPitchXCm(), readout_.layerPitchYCm());
      const auto distance = response_.distanceCm(
          payload.driftTimeNs, payload.driftAngleRadians, maximumDistance);
      if (!distance) {
        throw std::runtime_error("OD drift time cannot be inverted");
      }
      const auto truth = truthByDigi.find(digi.getObjectID().index);
      if (truth == truthByDigi.end() || truth->second.empty()) {
        throw std::runtime_error("OD digit has no simulated-hit provenance");
      }
      const auto &tube = readout_.tube(channel);
      const auto wire = readout_.wirePosition(tube, payload.zCm);
      const auto measuredDirection =
          readout_.localToGlobalDirection(channel.plank, 1.0, 0.0);
      const auto measuredPhi =
          std::atan2(measuredDirection[1], measuredDirection[0]);

      for (const auto side : {simulation::OuterDetectorDriftSide::Negative,
                              simulation::OuterDetectorDriftSide::Positive}) {
        const auto sign =
            side == simulation::OuterDetectorDriftSide::Negative ? -1.0 : 1.0;
        auto hit = output.create();
        hit.setCellID(simulation::OuterDetectorReadoutGeometry::encodeCellID(
            {channel.plank, channel.layer, channel.column, side}));
        hit.setType(static_cast<int>(side));
        hit.setQuality(0);
        hit.setTime(static_cast<float>(payload.driftTimeNs));
        hit.setEDep(0.0F);
        hit.setEDepError(0.0F);
        hit.setU({static_cast<float>(std::acos(-1.0) / 2.0),
                  static_cast<float>(measuredPhi)});
        hit.setV({0.0F, 0.0F});
        hit.setDu(static_cast<float>(10.0 * readout_.transverseResolutionCm()));
        hit.setDv(static_cast<float>(10.0 * readout_.longitudinalResolutionCm()));
        hit.setPosition({10.0 * (wire[0] + sign * *distance * measuredDirection[0]),
                         10.0 * (wire[1] + sign * *distance * measuredDirection[1]),
                         10.0 * payload.zCm});
        hit.setCovMatrix(std::array<float, 6>{});
        for (const auto &contribution : truth->second) {
          auto link = truthOutput.create();
          link.setFrom(hit);
          link.setTo(contribution.hit);
          link.setWeight(contribution.weight);
        }
      }
    }

    c4h::setCollectionID(output, event, *this, outputToken_);
    c4h::setCollectionID(truthOutput, event, *this, truthOutputToken_);
    event.emplace(outputToken_, std::move(output));
    event.emplace(truthOutputToken_, std::move(truthOutput));
  }

  const edm::EDGetTokenT<edm4hep::RawTimeSeriesCollection> inputToken_;
  const edm::EDGetTokenT<RawTimeSeriesSimTrackerHitLinkCollection>
      truthInputToken_;
  const edm::EDPutTokenT<edm4hep::TrackerHitPlaneCollection> outputToken_;
  const edm::EDPutTokenT<edm4hep::TrackerHitSimTrackerHitLinkCollection>
      truthOutputToken_;
  const simulation::OuterDetectorReadoutGeometry readout_;
  const simulation::OuterDetectorDriftResponse response_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiOuterDetectorHitReconstructionProducer);
