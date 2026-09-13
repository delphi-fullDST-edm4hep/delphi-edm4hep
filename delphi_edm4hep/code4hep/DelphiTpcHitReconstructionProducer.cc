#include "delphi_edm4hep/Code4hep/TpcDigiSimTrackerHitLinkCollection.h"
#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GeometryModel.h"
#include "delphi_edm4hep/Simulation/TpcDigitizationConditions.h"
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
#include "edm4hep/TimeSeriesCollection.h"
#include "edm4hep/TrackerHit3DCollection.h"
#include "edm4hep/TrackerHitSimTrackerHitLinkCollection.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace delphi_edm4hep {
namespace {

struct TpcReconstructionModels {
  simulation::TpcReadoutGeometry readout;
  simulation::TpcDigitizationConditions conditions;
};

TpcReconstructionModels readModels(const std::string &snapshot) {
  const auto database = geometry::CargoDatabase::readFile(snapshot);
  const auto geometry = geometry::GeometryModel::fromCargo(database, snapshot);
  auto readout = simulation::TpcReadoutGeometry::fromCargo(database, geometry);
  auto conditions =
      simulation::TpcDigitizationConditions::fromCargo(database, readout);
  return {std::move(readout), std::move(conditions)};
}

} // namespace

class DelphiTpcHitReconstructionProducer final
    : public edm::global::EDProducer<> {
public:
  explicit DelphiTpcHitReconstructionProducer(const edm::ParameterSet &config)
      : inputToken_(consumes(config.getParameter<edm::InputTag>("digis"))),
        truthInputToken_(consumes(
            config.getParameter<edm::InputTag>("digiTruthLinks"))),
        outputToken_(produces<edm4hep::TrackerHit3DCollection>("TpcHits")),
        truthOutputToken_(
            produces<edm4hep::TrackerHitSimTrackerHitLinkCollection>(
                "TpcHitSimTrackerHitLinks")),
        models_(readModels(config.getParameter<std::string>("cargoSnapshot"))) {}

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
      const auto digi = link.getFrom();
      const auto simHit = link.getTo();
      if (!digi.isAvailable() || !simHit.isAvailable()) {
        throw std::runtime_error("TPC digi truth link is unresolved");
      }
      truthByDigi[digi.getObjectID().index].push_back(
          {simHit, link.getWeight()});
    }

    edm4hep::TrackerHit3DCollection output;
    edm4hep::TrackerHitSimTrackerHitLinkCollection truthOutput;
    for (const auto digi : event.get(inputToken_)) {
      if (digi.amplitude_size() == 0 || digi.getInterval() <= 0) {
        continue;
      }
      const auto address =
          simulation::TpcReadoutGeometry::decodeCellId(digi.getCellID());
      const auto peak = std::max_element(digi.amplitude_begin(),
                                         digi.amplitude_end());
      const auto peakIndex = static_cast<double>(
          std::distance(digi.amplitude_begin(), peak));
      const auto peakTimeNs = digi.getTime() + peakIndex * digi.getInterval();
      const auto &sector = models_.conditions.sector(address.sector);
      auto absoluteZCm =
          models_.readout.driftHalfLengthCm() -
          peakTimeNs / 1000.0 * sector.driftVelocityCmPerMicrosecond;
      absoluteZCm =
          std::clamp(absoluteZCm, 0.0, models_.readout.driftHalfLengthCm());
      const auto zCm = address.endcap == 0
                           ? -std::max(absoluteZCm, 1e-12)
                           : absoluteZCm;
      const auto position = models_.readout.padCenter(address, zCm);
      const auto row = std::find_if(
          models_.readout.rows().begin(), models_.readout.rows().end(),
          [&](const auto &entry) { return entry.number == address.row; });
      if (row == models_.readout.rows().end()) {
        throw std::runtime_error("TPC digi refers to an unknown row");
      }
      const auto sectorTransform = std::find_if(
          models_.readout.sectors().begin(), models_.readout.sectors().end(),
          [&](const auto &entry) {
            return entry.readoutSector == address.sector &&
                   entry.endcap == address.endcap;
          });
      if (sectorTransform == models_.readout.sectors().end()) {
        throw std::runtime_error("TPC digi refers to an unknown sector");
      }
      const auto transverseSigmaMm = row->padWidthCm * 10.0 / std::sqrt(12.0);
      const auto radialSigmaMm = row->padHeightCm * 10.0 / std::sqrt(12.0);
      const auto zSigmaMm = sector.driftVelocityCmPerMicrosecond *
                            digi.getInterval() / 1000.0 * 10.0 /
                            std::sqrt(12.0);
      const auto radialX = position[0] - sectorTransform->translationXCm;
      const auto radialY = position[1] - sectorTransform->translationYCm;
      const auto radius = std::hypot(radialX, radialY);
      const auto ux = radialX / radius;
      const auto uy = radialY / radius;
      const auto tx = uy;
      const auto ty = -ux;
      const auto radialVariance = radialSigmaMm * radialSigmaMm;
      const auto transverseVariance = transverseSigmaMm * transverseSigmaMm;
      const auto covarianceXX =
          radialVariance * ux * ux + transverseVariance * tx * tx;
      const auto covarianceXY =
          radialVariance * ux * uy + transverseVariance * tx * ty;
      const auto covarianceYY =
          radialVariance * uy * uy + transverseVariance * ty * ty;

      auto hit = output.create();
      hit.setCellID(digi.getCellID());
      hit.setType(0);
      hit.setQuality(static_cast<int>(
          models_.conditions.pad(address.sector, address.row, address.pad)
              .status));
      hit.setTime(static_cast<float>(peakTimeNs));
      hit.setEDep(0);
      hit.setEDepError(0);
      hit.setPosition({10.0 * position[0], 10.0 * position[1],
                       10.0 * position[2]});
      hit.setCovMatrix(std::array<float, 6>{
          static_cast<float>(covarianceXX), static_cast<float>(covarianceXY),
          static_cast<float>(covarianceYY), 0.0F, 0.0F,
          static_cast<float>(zSigmaMm * zSigmaMm)});
      const auto truth = truthByDigi.find(digi.getObjectID().index);
      if (truth == truthByDigi.end() || truth->second.empty()) {
        throw std::runtime_error("TPC digi has no simulated-hit provenance");
      }
      for (const auto &contribution : truth->second) {
        auto link = truthOutput.create();
        link.setFrom(hit);
        link.setTo(contribution.hit);
        link.setWeight(contribution.weight);
      }
    }
    c4h::setCollectionID(output, event, *this, outputToken_);
    c4h::setCollectionID(truthOutput, event, *this, truthOutputToken_);
    event.emplace(outputToken_, std::move(output));
    event.emplace(truthOutputToken_, std::move(truthOutput));
  }

  const edm::EDGetTokenT<edm4hep::TimeSeriesCollection> inputToken_;
  const edm::EDGetTokenT<TpcDigiSimTrackerHitLinkCollection>
      truthInputToken_;
  const edm::EDPutTokenT<edm4hep::TrackerHit3DCollection> outputToken_;
  const edm::EDPutTokenT<edm4hep::TrackerHitSimTrackerHitLinkCollection>
      truthOutputToken_;
  const TpcReconstructionModels models_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiTpcHitReconstructionProducer);
