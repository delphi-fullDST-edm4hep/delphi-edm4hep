#include "delphi_edm4hep/Event/ParticleCounts.h"

#include "Code4hep/IOUtilities/FrameParameterConversion.h"
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
#include "podio/UserDataCollection.h"

#include <cstdint>

namespace delphi_edm4hep {

class DelphiEventSummaryProducer final : public edm::global::EDProducer<> {
public:
  explicit DelphiEventSummaryProducer(const edm::ParameterSet& config)
      : chargeCodesToken_(consumes(
            config.getParameter<edm::InputTag>("chargeCodes"))),
        nChargedToken_(produces<podio::UserDataCollection<std::int32_t>>(
            c4h::frameParameterCollectionName('I',
                                              "native_EVT_nCharged"))),
        nNeutralToken_(produces<podio::UserDataCollection<std::int32_t>>(
            c4h::frameParameterCollectionName('I',
                                              "native_EVT_nNeutral"))) {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("chargeCodes");
    descriptions.addDefault(description);
  }

private:
  void produce(edm::StreamID, edm::Event& event,
               const edm::EventSetup&) const final {
    const auto counts = event::countParticleChargeCodes(
        event.get(chargeCodesToken_));
    event.emplace(
        nChargedToken_,
        podio::UserDataCollection<std::int32_t>{{counts.charged}});
    event.emplace(
        nNeutralToken_,
        podio::UserDataCollection<std::int32_t>{{counts.neutral}});
  }

  const edm::EDGetTokenT<podio::UserDataCollection<std::int32_t>>
      chargeCodesToken_;
  const edm::EDPutTokenT<podio::UserDataCollection<std::int32_t>>
      nChargedToken_;
  const edm::EDPutTokenT<podio::UserDataCollection<std::int32_t>>
      nNeutralToken_;
};

}  // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiEventSummaryProducer);
