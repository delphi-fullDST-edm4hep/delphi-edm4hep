#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GeometryModel.h"
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
#include "edm4hep/SimTrackerHitCollection.h"
#include "edm4hep/TrackerHit3DCollection.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <utility>

namespace delphi_edm4hep {

class DelphiTpcPadMapperProducer final : public edm::global::EDProducer<> {
public:
  explicit DelphiTpcPadMapperProducer(const edm::ParameterSet &config)
      : inputToken_(
            consumes(config.getParameter<edm::InputTag>("simTrackerHits"))),
        outputToken_(produces<edm4hep::TrackerHit3DCollection>("TpcPadHits")),
        readout_(
            readReadout(config.getParameter<std::string>("cargoSnapshot"))) {}

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("simTrackerHits");
    description.add<std::string>("cargoSnapshot");
    descriptions.addDefault(description);
  }

private:
  static simulation::TpcReadoutGeometry
  readReadout(const std::string &snapshot) {
    const auto database = geometry::CargoDatabase::readFile(snapshot);
    const auto model = geometry::GeometryModel::fromCargo(database, snapshot);
    return simulation::TpcReadoutGeometry::fromCargo(database, model);
  }

  void produce(edm::StreamID, edm::Event &event,
               const edm::EventSetup &) const final {
    edm4hep::TrackerHit3DCollection output;
    for (const auto hit : event.get(inputToken_)) {
      const auto &position = hit.getPosition();
      const auto address = readout_.locatePad(
          position[0] / 10.0, position[1] / 10.0, position[2] / 10.0);
      if (!address) {
        continue;
      }
      auto mapped = output.create();
      mapped.setCellID(simulation::TpcReadoutGeometry::encodeCellId(*address));
      mapped.setType(0);
      mapped.setQuality(hit.getQuality());
      mapped.setTime(hit.getTime());
      mapped.setEDep(hit.getEDep());
      mapped.setEDepError(0);
      mapped.setPosition(position);
      mapped.setCovMatrix({});
    }
    c4h::setCollectionID(output, event, *this, outputToken_);
    event.emplace(outputToken_, std::move(output));
  }

  const edm::EDGetTokenT<edm4hep::SimTrackerHitCollection> inputToken_;
  const edm::EDPutTokenT<edm4hep::TrackerHit3DCollection> outputToken_;
  const simulation::TpcReadoutGeometry readout_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiTpcPadMapperProducer);
