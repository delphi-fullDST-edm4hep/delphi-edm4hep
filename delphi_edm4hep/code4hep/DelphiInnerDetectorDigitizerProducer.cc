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
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"
#include "edm4hep/RawTimeSeriesCollection.h"
#include "edm4hep/SimTrackerHitCollection.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <random>
#include <string>
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

std::uint64_t eventSeed(std::uint32_t baseSeed, std::uint32_t run,
                        std::uint64_t event) {
  std::uint64_t value = static_cast<std::uint64_t>(baseSeed) ^
                        (static_cast<std::uint64_t>(run) << 32U) ^ event;
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

std::pair<std::array<double, 3>, std::array<double, 3>>
segmentCm(edm4hep::SimTrackerHit hit) {
  const auto &position = hit.getPosition();
  const auto &momentum = hit.getMomentum();
  const auto magnitude =
      std::sqrt(momentum[0] * momentum[0] + momentum[1] * momentum[1] +
                momentum[2] * momentum[2]);
  const std::array<double, 3> start{position[0] / 10.0, position[1] / 10.0,
                                    position[2] / 10.0};
  auto end = start;
  if (magnitude > 0 && hit.getPathLength() > 0) {
    const auto lengthCm = hit.getPathLength() / 10.0;
    for (std::size_t coordinate = 0; coordinate < end.size(); ++coordinate) {
      end[coordinate] += lengthCm * momentum[coordinate] / magnitude;
    }
  }
  return {start, end};
}

struct Candidate {
  simulation::InnerDetectorJetAddress address;
  std::size_t simHitIndex{};
  double driftTimeNs{};
};

} // namespace

class DelphiInnerDetectorDigitizerProducer final
    : public edm::global::EDProducer<> {
public:
  explicit DelphiInnerDetectorDigitizerProducer(const edm::ParameterSet &config)
      : inputToken_(
            consumes(config.getParameter<edm::InputTag>("simTrackerHits"))),
        outputToken_(produces<edm4hep::RawTimeSeriesCollection>(
            "InnerDetectorJetDigis")),
        truthOutputToken_(produces<RawTimeSeriesSimTrackerHitLinkCollection>(
            "InnerDetectorDigiSimTrackerHitLinks")),
        randomSeed_(config.getParameter<unsigned int>("randomSeed")),
        wireEfficiency_(config.getParameter<double>("wireEfficiency")),
        transverseResolutionCm_(
            config.getParameter<double>("transverseResolutionCm")),
        models_(readModels(config.getParameter<std::string>("cargoSnapshot"),
                           config.getParameter<double>("magneticFieldTesla"))) {
    if (randomSeed_ == 0 || wireEfficiency_ < 0 || wireEfficiency_ > 1 ||
        transverseResolutionCm_ < 0) {
      throw cms::Exception("Configuration")
          << "invalid DELPHI ID digitizer response configuration";
    }
  }

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("simTrackerHits");
    description.add<std::string>("cargoSnapshot");
    description.add<double>("magneticFieldTesla", 1.2312434);
    description.add<unsigned int>("randomSeed", 24680U);
    description.add<double>("wireEfficiency", 0.80);
    description.add<double>("transverseResolutionCm", 0.0100);
    descriptions.addDefault(description);
  }

private:
  void produce(edm::StreamID, edm::Event &event,
               const edm::EventSetup &) const final {
    const auto &simHits = event.get(inputToken_);
    std::map<std::uint64_t, std::vector<Candidate>> candidates;
    std::mt19937_64 engine(
        eventSeed(randomSeed_, event.id().run(), event.id().event()));
    std::normal_distribution<double> normal;
    std::uniform_real_distribution<double> uniform;

    for (std::size_t hitIndex = 0; hitIndex < simHits.size(); ++hitIndex) {
      const auto hit = simHits[hitIndex];
      if (!hit.getParticle().isAvailable()) {
        throw cms::Exception("DataCorruption")
            << "ID simulated hit " << hitIndex << " has no MC particle";
      }
      const auto [start, end] = segmentCm(hit);
      for (const auto &crossing :
           models_.readout.jetWireCrossings(start, end)) {
        const auto &wire =
            models_.readout.jetSectors()[crossing.address.sector - 1]
                .wires[crossing.address.wire - 1];
        if (wire.status != 0) {
          continue;
        }
        auto phi = crossing.address.localPhiRadians +
                   normal(engine) * transverseResolutionCm_ / wire.radiusCm;
        if (phi * crossing.address.localPhiRadians < 0) {
          phi = 0;
        }
        const auto sectorWidth = 2.0 * std::numbers::pi / 24.0;
        phi = std::clamp(phi, -sectorWidth, sectorWidth);
        auto address = crossing.address;
        address.localPhiRadians = phi;
        const auto driftTime = models_.response.driftTimeNs(
            address.sector, address.wire, address.side, phi);
        const auto channel =
            simulation::InnerDetectorReadoutGeometry::encodeJetChannelID(
                {address.sector, address.wire});
        candidates[channel].push_back({address, hitIndex, driftTime});
      }
    }

    edm4hep::RawTimeSeriesCollection output;
    RawTimeSeriesSimTrackerHitLinkCollection truthOutput;
    const auto deadTimeNs = 1000.0 * models_.readout.deadTimeMicroseconds();
    constexpr auto maximumTdcCount = (1 << 14) - 1;
    constexpr auto tdcIntervalNs = 1000.0 / ((351.0 / 3.0) * 4.0);
    for (auto &[cellID, channelCandidates] : candidates) {
      std::sort(channelCandidates.begin(), channelCandidates.end(),
                [](const auto &left, const auto &right) {
                  return left.driftTimeNs < right.driftTimeNs;
                });
      auto previousAcceptedTime = -std::numeric_limits<double>::infinity();
      for (const auto &candidate : channelCandidates) {
        if (candidate.driftTimeNs - previousAcceptedTime < deadTimeNs ||
            uniform(engine) >= wireEfficiency_) {
          continue;
        }
        previousAcceptedTime = candidate.driftTimeNs;
        const auto rawCount = models_.response.tdcCount(
            candidate.address.sector, candidate.address.wire,
            candidate.driftTimeNs);
        const auto count = std::clamp(rawCount, 0, maximumTdcCount);
        const auto quantizedTime = models_.response.driftTimeFromTdcCount(
            candidate.address.sector, candidate.address.wire, count);

        auto digi = output.create();
        digi.setCellID(cellID);
        digi.setQuality(0);
        digi.setTime(static_cast<float>(quantizedTime));
        digi.setCharge(0.0F);
        digi.setInterval(static_cast<float>(tdcIntervalNs));
        digi.addToAdcCounts(count);

        auto link = truthOutput.create();
        link.setFrom(digi);
        link.setTo(simHits.at(candidate.simHitIndex));
        link.setWeight(1.0F);
      }
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
  const double wireEfficiency_;
  const double transverseResolutionCm_;
  const InnerDetectorModels models_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiInnerDetectorDigitizerProducer);
