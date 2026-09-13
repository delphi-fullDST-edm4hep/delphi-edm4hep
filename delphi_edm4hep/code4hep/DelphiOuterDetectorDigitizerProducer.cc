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
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"
#include "edm4hep/RawTimeSeriesCollection.h"
#include "edm4hep/SimTrackerHitCollection.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <utility>

namespace delphi_edm4hep {
namespace {

std::uint64_t eventSeed(std::uint32_t baseSeed, std::uint32_t run,
                        std::uint64_t event) {
  std::uint64_t value = static_cast<std::uint64_t>(baseSeed) ^
                        (static_cast<std::uint64_t>(run) << 32U) ^ event;
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

struct Candidate {
  std::size_t hitIndex{};
  simulation::OuterDetectorPhysicalChannelPayload payload;
  double rawLeadingTimeNs{};
};

} // namespace

class DelphiOuterDetectorDigitizerProducer final
    : public edm::global::EDProducer<> {
public:
  explicit DelphiOuterDetectorDigitizerProducer(const edm::ParameterSet &config)
      : inputToken_(consumes(config.getParameter<edm::InputTag>("simTrackerHits"))),
        outputToken_(produces<edm4hep::RawTimeSeriesCollection>(
            "OuterDetectorDigis")),
        truthOutputToken_(produces<RawTimeSeriesSimTrackerHitLinkCollection>(
            "OuterDetectorDigiSimTrackerHitLinks")),
        randomSeed_(config.getParameter<unsigned int>("randomSeed")),
        readout_(simulation::OuterDetectorReadoutGeometry::fromCargo(
            geometry::CargoDatabase::readFile(
                config.getParameter<std::string>("cargoSnapshot")))),
        response_{} {
    if (randomSeed_ == 0) {
      throw cms::Exception("Configuration") << "OD random seed must be nonzero";
    }
  }

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("simTrackerHits");
    description.add<std::string>("cargoSnapshot");
    description.add<unsigned int>("randomSeed", 97531U);
    descriptions.addDefault(description);
  }

private:
  void produce(edm::StreamID, edm::Event &event,
               const edm::EventSetup &) const final {
    const auto &simHits = event.get(inputToken_);
    std::map<std::uint64_t, Candidate> candidates;
    std::mt19937_64 engine(
        eventSeed(randomSeed_, event.id().run(), event.id().event()));
    std::normal_distribution<double> normal;
    std::uniform_real_distribution<double> uniform;

    for (std::size_t index = 0; index < simHits.size(); ++index) {
      const auto hit = simHits[index];
      if (!hit.getParticle().isAvailable()) {
        throw cms::Exception("DataCorruption")
            << "OD simulated hit " << index << " has no MC particle";
      }
      const auto position = hit.getPosition();
      const auto located = readout_.locate(position[0] / 10.0,
                                           position[1] / 10.0,
                                           position[2] / 10.0);
      if (!located || !located->tube->active ||
          uniform(engine) >= located->tube->efficiency) {
        continue;
      }
      const auto smearedDistance =
          std::max(0.0, located->distanceCm +
                            normal(engine) * readout_.transverseResolutionCm());
      const auto measuredZ = std::clamp(
          position[2] / 10.0 +
              normal(engine) * readout_.longitudinalResolutionCm(),
          located->tube->zNegativeCm, located->tube->zPositiveCm);
      const auto driftTime =
          response_.driftTimeNs(smearedDistance, located->angleRadians);
      const auto zFraction =
          (located->tube->zPositiveCm - measuredZ) /
          (located->tube->zPositiveCm - located->tube->zNegativeCm);
      const auto rawTime = driftTime + located->tube->pedestalNs +
                           located->tube->zPropagationNs * zFraction +
                           hit.getTime();
      const auto cellID = simulation::OuterDetectorReadoutGeometry::encodeChannelID(
          located->tube->channel);
      Candidate candidate{index,
                          {driftTime, measuredZ, located->angleRadians,
                           located->tube->pulseWidthNs},
                          rawTime};
      const auto previous = candidates.find(cellID);
      if (previous == candidates.end() ||
          candidate.payload.driftTimeNs < previous->second.payload.driftTimeNs) {
        candidates[cellID] = candidate;
      }
    }

    edm4hep::RawTimeSeriesCollection output;
    RawTimeSeriesSimTrackerHitLinkCollection truthOutput;
    for (const auto &[cellID, candidate] : candidates) {
      auto digi = output.create();
      digi.setCellID(cellID);
      digi.setQuality(simulation::outerDetectorPhysicalChannelPayloadVersion);
      digi.setTime(static_cast<float>(candidate.rawLeadingTimeNs));
      digi.setCharge(0.0F);
      digi.setInterval(simulation::outerDetectorPayloadIntervalNs);
      for (const auto word : simulation::encodeOuterDetectorPhysicalChannelPayload(
               candidate.payload)) {
        digi.addToAdcCounts(word);
      }
      auto link = truthOutput.create();
      link.setFrom(digi);
      link.setTo(simHits.at(candidate.hitIndex));
      link.setWeight(1.0F);
    }

    c4h::setCollectionID(output, event, *this, outputToken_);
    c4h::setCollectionID(truthOutput, event, *this, truthOutputToken_);
    event.emplace(outputToken_, std::move(output));
    event.emplace(truthOutputToken_, std::move(truthOutput));
  }

  const edm::EDGetTokenT<edm4hep::SimTrackerHitCollection> inputToken_;
  const edm::EDPutTokenT<edm4hep::RawTimeSeriesCollection> outputToken_;
  const edm::EDPutTokenT<RawTimeSeriesSimTrackerHitLinkCollection>
      truthOutputToken_;
  const std::uint32_t randomSeed_;
  const simulation::OuterDetectorReadoutGeometry readout_;
  const simulation::OuterDetectorDriftResponse response_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiOuterDetectorDigitizerProducer);
