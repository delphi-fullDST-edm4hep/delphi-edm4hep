#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/EDGetToken.h"
#include "FWCore/Utilities/interface/EDPutToken.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"
#include "edm4hep/SimTrackerHitCollection.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <array>
#include <cstdint>
#include <utility>

namespace delphi_edm4hep {
namespace {

void copyHit(edm4hep::SimTrackerHit source,
             edm4hep::SimTrackerHitCollection &destination) {
  auto hit = destination.create();
  hit.setCellID(source.getCellID());
  hit.setEDep(source.getEDep());
  hit.setTime(source.getTime());
  hit.setPathLength(source.getPathLength());
  hit.setQuality(source.getQuality());
  hit.setPosition(source.getPosition());
  hit.setMomentum(source.getMomentum());
  const auto particle = source.getParticle();
  if (particle.isAvailable()) {
    hit.setParticle(particle);
  }
}

} // namespace

class DelphiTrackerHitPartitionProducer final
    : public edm::global::EDProducer<> {
public:
  explicit DelphiTrackerHitPartitionProducer(const edm::ParameterSet &config)
      : inputToken_(
            consumes(config.getParameter<edm::InputTag>("simTrackerHits"))),
        vertexToken_(
            produces<edm4hep::SimTrackerHitCollection>("VertexSimHits")),
        innerToken_(
            produces<edm4hep::SimTrackerHitCollection>("InnerDetectorSimHits")),
        tpcToken_(produces<edm4hep::SimTrackerHitCollection>("TpcSimHits")),
        outerToken_(produces<edm4hep::SimTrackerHitCollection>(
            "OuterDetectorSimHits")) {}

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("simTrackerHits");
    descriptions.addDefault(description);
  }

private:
  void produce(edm::StreamID, edm::Event &event,
               const edm::EventSetup &) const final {
    std::array<edm4hep::SimTrackerHitCollection, 4> outputs;
    for (const auto source : event.get(inputToken_)) {
      const auto subsystem =
          static_cast<std::uint8_t>(source.getCellID() >> 56U);
      if (subsystem < 1 || subsystem > outputs.size()) {
        throw cms::Exception("DataCorruption")
            << "DELPHI tracker hit has unknown subsystem "
            << static_cast<unsigned int>(subsystem) << " in cell ID "
            << source.getCellID();
      }
      copyHit(source, outputs[subsystem - 1]);
    }

    put(event, vertexToken_, std::move(outputs[0]));
    put(event, innerToken_, std::move(outputs[1]));
    put(event, tpcToken_, std::move(outputs[2]));
    put(event, outerToken_, std::move(outputs[3]));
  }

  template <typename Token>
  void put(edm::Event &event, const Token &token,
           edm4hep::SimTrackerHitCollection output) const {
    c4h::setCollectionID(output, event, *this, token);
    event.emplace(token, std::move(output));
  }

  const edm::EDGetTokenT<edm4hep::SimTrackerHitCollection> inputToken_;
  const edm::EDPutTokenT<edm4hep::SimTrackerHitCollection> vertexToken_;
  const edm::EDPutTokenT<edm4hep::SimTrackerHitCollection> innerToken_;
  const edm::EDPutTokenT<edm4hep::SimTrackerHitCollection> tpcToken_;
  const edm::EDPutTokenT<edm4hep::SimTrackerHitCollection> outerToken_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiTrackerHitPartitionProducer);
