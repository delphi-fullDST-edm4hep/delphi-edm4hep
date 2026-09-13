#include "delphi_edm4hep/Code4hep/RawTimeSeriesSimTrackerHitLinkCollection.h"
#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GeometryModel.h"
#include "delphi_edm4hep/Simulation/VertexReadoutGeometry.h"
#include "delphi_edm4hep/Simulation/VertexStripReadout.h"

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

simulation::VertexReadoutGeometry readGeometry(const std::string &snapshot) {
  const auto database = geometry::CargoDatabase::readFile(snapshot);
  const auto geometry = geometry::GeometryModel::fromCargo(database, snapshot);
  return simulation::VertexReadoutGeometry::fromCargo(database, geometry);
}

std::array<double, 3>
localAxis(const simulation::VertexRigidTransform &transform,
          std::size_t localCoordinate) {
  return {transform.rotation[localCoordinate],
          transform.rotation[3 + localCoordinate],
          transform.rotation[6 + localCoordinate]};
}

edm4hep::Vector2f sphericalDirection(const std::array<double, 3> &axis) {
  const auto magnitude =
      std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
  return {
      static_cast<float>(std::acos(std::clamp(axis[2] / magnitude, -1.0, 1.0))),
      static_cast<float>(std::atan2(axis[1], axis[0]))};
}

} // namespace

class DelphiVertexHitReconstructionProducer final
    : public edm::global::EDProducer<> {
public:
  explicit DelphiVertexHitReconstructionProducer(
      const edm::ParameterSet &config)
      : inputToken_(consumes(config.getParameter<edm::InputTag>("digis"))),
        truthInputToken_(
            consumes(config.getParameter<edm::InputTag>("digiTruthLinks"))),
        outputToken_(
            produces<edm4hep::TrackerHitPlaneCollection>("VertexHits")),
        truthOutputToken_(
            produces<edm4hep::TrackerHitSimTrackerHitLinkCollection>(
                "VertexHitSimTrackerHitLinks")),
        readout_(
            readGeometry(config.getParameter<std::string>("cargoSnapshot"))) {}

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
        throw std::runtime_error("VD digit truth link is unresolved");
      }
      truthByDigi[digi.getObjectID().index].push_back(
          {simHit, link.getWeight()});
    }

    edm4hep::TrackerHitPlaneCollection output;
    edm4hep::TrackerHitSimTrackerHitLinkCollection truthOutput;
    const simulation::VertexStripReadout stripReadout;
    for (const auto digi : event.get(inputToken_)) {
      if (digi.adcCounts_size() != 1) {
        throw std::runtime_error("VD digit has an invalid ADC payload");
      }
      const auto address =
          simulation::VertexStripReadout::decodeCellID(digi.getCellID());
      const auto &sensor = readout_.sensor(address.semanticSensor);
      const auto position =
          stripReadout.measurementCenter(sensor, address.side, address.strip);
      const auto measuredCoordinate =
          address.side == simulation::VertexReadoutSide::P ? 0U : 2U;
      const auto unmeasuredCoordinate =
          address.side == simulation::VertexReadoutSide::P ? 2U : 0U;
      const auto measuredAxis =
          localAxis(sensor.pTransform, measuredCoordinate);
      const auto unmeasuredAxis =
          localAxis(sensor.pTransform, unmeasuredCoordinate);
      const auto measuredPitchCm =
          address.side == simulation::VertexReadoutSide::P
              ? sensor.readout.pReadoutPitchCm
              : (sensor.readout.nSecondPitchCm > 0 &&
                         address.strip > sensor.readout.nFirstPitchChannels
                     ? sensor.readout.nSecondPitchCm
                     : sensor.readout.nFirstPitchCm);
      const auto unmeasuredLengthCm =
          address.side == simulation::VertexReadoutSide::P
              ? sensor.pActiveLine.lengthCm()
              : sensor.nActiveLine->lengthCm();

      auto hit = output.create();
      hit.setCellID(digi.getCellID());
      hit.setType(static_cast<int>(address.side));
      hit.setQuality(digi.getQuality());
      hit.setTime(digi.getTime());
      hit.setEDep(0.0F);
      hit.setEDepError(0.0F);
      hit.setU(sphericalDirection(measuredAxis));
      hit.setV(sphericalDirection(unmeasuredAxis));
      hit.setDu(static_cast<float>(10.0 * measuredPitchCm / std::sqrt(12.0)));
      hit.setDv(
          static_cast<float>(10.0 * unmeasuredLengthCm / std::sqrt(12.0)));
      hit.setPosition(
          {10.0 * position[0], 10.0 * position[1], 10.0 * position[2]});
      hit.setCovMatrix(std::array<float, 6>{});

      const auto truth = truthByDigi.find(digi.getObjectID().index);
      if (truth == truthByDigi.end() || truth->second.empty()) {
        throw std::runtime_error("VD digit has no simulated-hit provenance");
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

  const edm::EDGetTokenT<edm4hep::RawTimeSeriesCollection> inputToken_;
  const edm::EDGetTokenT<RawTimeSeriesSimTrackerHitLinkCollection>
      truthInputToken_;
  const edm::EDPutTokenT<edm4hep::TrackerHitPlaneCollection> outputToken_;
  const edm::EDPutTokenT<edm4hep::TrackerHitSimTrackerHitLinkCollection>
      truthOutputToken_;
  const simulation::VertexReadoutGeometry readout_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiVertexHitReconstructionProducer);
