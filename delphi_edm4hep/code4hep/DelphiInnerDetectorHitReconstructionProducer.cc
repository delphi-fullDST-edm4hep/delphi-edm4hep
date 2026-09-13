#include "delphi_edm4hep/Code4hep/RawTimeSeriesSimTrackerHitLinkCollection.h"
#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Simulation/InnerDetectorJetResponse.h"
#include "delphi_edm4hep/Simulation/InnerDetectorReadoutGeometry.h"

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
#include <numbers>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace delphi_edm4hep {
namespace {

struct InnerDetectorModels {
  simulation::InnerDetectorReadoutGeometry readout;
  simulation::InnerDetectorJetResponse response;
};

InnerDetectorModels readModels(const std::string &snapshot,
                               double magneticFieldTesla) {
  const auto database = geometry::CargoDatabase::readFile(snapshot);
  auto readout = simulation::InnerDetectorReadoutGeometry::fromCargo(database);
  auto response = simulation::InnerDetectorJetResponse::fromCargo(
      database, readout, magneticFieldTesla);
  return {std::move(readout), std::move(response)};
}

} // namespace

class DelphiInnerDetectorHitReconstructionProducer final
    : public edm::global::EDProducer<> {
public:
  explicit DelphiInnerDetectorHitReconstructionProducer(
      const edm::ParameterSet &config)
      : inputToken_(consumes(config.getParameter<edm::InputTag>("digis"))),
        truthInputToken_(
            consumes(config.getParameter<edm::InputTag>("digiTruthLinks"))),
        outputToken_(produces<edm4hep::TrackerHitPlaneCollection>(
            "InnerDetectorJetHits")),
        truthOutputToken_(
            produces<edm4hep::TrackerHitSimTrackerHitLinkCollection>(
                "InnerDetectorHitSimTrackerHitLinks")),
        transverseResolutionCm_(
            config.getParameter<double>("transverseResolutionCm")),
        models_(readModels(config.getParameter<std::string>("cargoSnapshot"),
                           config.getParameter<double>("magneticFieldTesla"))) {
    if (transverseResolutionCm_ <= 0) {
      throw std::invalid_argument(
          "ID hit reconstruction requires positive transverse resolution");
    }
  }

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("digis");
    description.add<edm::InputTag>("digiTruthLinks");
    description.add<std::string>("cargoSnapshot");
    description.add<double>("magneticFieldTesla", 1.2312434);
    description.add<double>("transverseResolutionCm", 0.0100);
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
      const auto digi = link.getFrom();
      const auto simHit = link.getTo();
      if (!digi.isAvailable() || !simHit.isAvailable()) {
        throw std::runtime_error("ID digit truth link is unresolved");
      }
      truthByDigi[digi.getObjectID().index].push_back(
          {simHit, link.getWeight()});
    }

    edm4hep::TrackerHitPlaneCollection output;
    edm4hep::TrackerHitSimTrackerHitLinkCollection truthOutput;
    for (const auto digi : event.get(inputToken_)) {
      if (digi.adcCounts_size() != 1) {
        throw std::runtime_error("ID jet digit has an invalid TDC payload");
      }
      const auto channel =
          simulation::InnerDetectorReadoutGeometry::decodeJetChannelID(
              digi.getCellID());
      const auto driftTime = models_.response.driftTimeFromTdcCount(
          channel.sector, channel.wire, digi.getAdcCounts(0));
      const auto truth = truthByDigi.find(digi.getObjectID().index);
      if (truth == truthByDigi.end() || truth->second.empty()) {
        throw std::runtime_error(
            "ID jet digit has no simulated-hit provenance");
      }

      for (const auto side : {simulation::InnerDetectorDriftSide::Left,
                              simulation::InnerDetectorDriftSide::Right}) {
        const auto coordinate = models_.response.coordinateFromDriftTime(
            channel.sector, channel.wire, side, driftTime);
        if (!coordinate ||
            std::abs(coordinate->localPhiRadians) >
                std::numbers::pi / 24.0 + 1e-6 ||
            (side == simulation::InnerDetectorDriftSide::Left &&
             coordinate->localPhiRadians > 1e-6) ||
            (side == simulation::InnerDetectorDriftSide::Right &&
             coordinate->localPhiRadians < -1e-6)) {
          continue;
        }
        const auto globalPhi = models_.readout.jetSectorMidPhi(channel.sector) +
                               coordinate->localPhiRadians;
        simulation::InnerDetectorJetAddress address{
            channel.sector, channel.wire, side, coordinate->localPhiRadians};
        auto hit = output.create();
        hit.setCellID(
            simulation::InnerDetectorReadoutGeometry::encodeJetCellID(address));
        hit.setType(static_cast<int>(side));
        hit.setQuality(digi.getQuality());
        hit.setTime(static_cast<float>(driftTime));
        hit.setEDep(0.0F);
        hit.setEDepError(0.0F);
        hit.setU({static_cast<float>(std::numbers::pi / 2.0),
                  static_cast<float>(globalPhi + std::numbers::pi / 2.0)});
        hit.setV({0.0F, 0.0F});
        hit.setDu(static_cast<float>(10.0 * transverseResolutionCm_));
        hit.setDv(static_cast<float>(
            20.0 *
            models_.readout.jetSectors()[channel.sector - 1].halfLengthCm /
            std::sqrt(12.0)));
        hit.setPosition({10.0 * coordinate->radiusCm * std::cos(globalPhi),
                         10.0 * coordinate->radiusCm * std::sin(globalPhi),
                         0.0});
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
  const double transverseResolutionCm_;
  const InnerDetectorModels models_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiInnerDetectorHitReconstructionProducer);
