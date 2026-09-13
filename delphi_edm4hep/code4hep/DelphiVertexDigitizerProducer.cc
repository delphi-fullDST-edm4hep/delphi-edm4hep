#include "delphi_edm4hep/Code4hep/RawTimeSeriesSimTrackerHitLinkCollection.h"
#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GeometryModel.h"
#include "delphi_edm4hep/Simulation/VertexChannelResponse.h"
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
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"
#include "edm4hep/RawTimeSeriesCollection.h"
#include "edm4hep/SimTrackerHitCollection.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace delphi_edm4hep {
namespace {

struct VertexModels {
  simulation::VertexDigitizationConditions conditions;
  simulation::VertexReadoutGeometry readout;
};

VertexModels readModels(const std::string &snapshot) {
  const auto database = geometry::CargoDatabase::readFile(snapshot);
  const auto geometry = geometry::GeometryModel::fromCargo(database, snapshot);
  auto conditions = simulation::VertexDigitizationConditions::legacyV94c();
  auto readout = simulation::VertexReadoutGeometry::fromCargo(
      database, geometry, conditions);
  return {std::move(conditions), std::move(readout)};
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

std::array<double, 3> midpointCm(edm4hep::SimTrackerHit hit) {
  const auto &position = hit.getPosition();
  const auto &momentum = hit.getMomentum();
  const auto magnitude =
      std::sqrt(momentum[0] * momentum[0] + momentum[1] * momentum[1] +
                momentum[2] * momentum[2]);
  const auto halfPathMm = 0.5 * hit.getPathLength();
  return {(position[0] +
           (magnitude > 0 ? halfPathMm * momentum[0] / magnitude : 0.0)) /
              10.0,
          (position[1] +
           (magnitude > 0 ? halfPathMm * momentum[1] / magnitude : 0.0)) /
              10.0,
          (position[2] +
           (magnitude > 0 ? halfPathMm * momentum[2] / magnitude : 0.0)) /
              10.0};
}

struct TrackSensorSide {
  std::uint32_t semanticSensor{};
  std::uint32_t particleCollection{};
  std::int32_t particleIndex{};
  simulation::VertexReadoutSide side{};

  bool operator<(const TrackSensorSide &other) const {
    return std::tie(semanticSensor, particleCollection, particleIndex, side) <
           std::tie(other.semanticSensor, other.particleCollection,
                    other.particleIndex, other.side);
  }
};

struct StepContribution {
  std::size_t simHitIndex{};
  simulation::VertexStripAddress address;
  double depositedEnergyGeV{};
  double timeNs{};
};

struct ChannelContribution {
  simulation::VertexStripAddress address;
  simulation::VertexBarrelLayer layer{};
  double depositedEnergyGeV{};
  double energyTimeGeVNs{};
  std::map<std::size_t, double> depositedEnergyBySimHit;
};

} // namespace

class DelphiVertexDigitizerProducer final : public edm::global::EDProducer<> {
public:
  explicit DelphiVertexDigitizerProducer(const edm::ParameterSet &config)
      : inputToken_(
            consumes(config.getParameter<edm::InputTag>("simTrackerHits"))),
        outputToken_(produces<edm4hep::RawTimeSeriesCollection>("VertexDigis")),
        truthOutputToken_(produces<RawTimeSeriesSimTrackerHitLinkCollection>(
            "VertexDigiSimTrackerHitLinks")),
        randomSeed_(config.getParameter<unsigned int>("randomSeed")),
        models_(readModels(config.getParameter<std::string>("cargoSnapshot"))),
        stripReadout_(models_.conditions), response_(models_.conditions) {
    if (randomSeed_ == 0) {
      throw cms::Exception("Configuration")
          << "DelphiVertexDigitizerProducer requires a nonzero random seed";
    }
  }

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("simTrackerHits");
    description.add<std::string>("cargoSnapshot");
    description.add<unsigned int>("randomSeed", 13579U);
    descriptions.addDefault(description);
  }

private:
  void produce(edm::StreamID, edm::Event &event,
               const edm::EventSetup &) const final {
    const auto &simHits = event.get(inputToken_);
    std::map<TrackSensorSide, std::vector<StepContribution>> trackSteps;

    for (std::size_t hitIndex = 0; hitIndex < simHits.size(); ++hitIndex) {
      const auto hit = simHits[hitIndex];
      if (hit.getEDep() <= 0) {
        continue;
      }
      const auto particle = hit.getParticle();
      if (!particle.isAvailable()) {
        throw cms::Exception("DataCorruption")
            << "VD simulated hit " << hitIndex << " has no MC particle";
      }
      const auto particleID = particle.getObjectID();
      const auto &sensor =
          models_.readout.sensorForTransportCellID(hit.getCellID());
      const auto midpoint = midpointCm(hit);
      for (const auto side : {simulation::VertexReadoutSide::P,
                              simulation::VertexReadoutSide::N}) {
        const auto address = stripReadout_.locate(sensor, side, midpoint);
        if (!address) {
          continue;
        }
        const TrackSensorSide key{sensor.semanticSensor,
                                  particleID.collectionID, particleID.index,
                                  side};
        trackSteps[key].push_back(
            {hitIndex, *address, hit.getEDep(), hit.getTime()});
      }
    }

    std::map<std::uint64_t, ChannelContribution> channels;
    for (const auto &[key, steps] : trackSteps) {
      if (steps.size() < models_.conditions.minimumActiveSteps()) {
        continue;
      }
      const auto &sensor = models_.readout.sensor(key.semanticSensor);
      for (const auto &step : steps) {
        const auto cellID =
            simulation::VertexStripReadout::encodeCellID(step.address);
        auto [channel, inserted] = channels.try_emplace(cellID);
        if (inserted) {
          channel->second.address = step.address;
          channel->second.layer = sensor.layer;
        }
        channel->second.depositedEnergyGeV += step.depositedEnergyGeV;
        channel->second.energyTimeGeVNs +=
            step.depositedEnergyGeV * step.timeNs;
        channel->second.depositedEnergyBySimHit[step.simHitIndex] +=
            step.depositedEnergyGeV;
      }
    }

    std::mt19937_64 engine(
        eventSeed(randomSeed_, event.id().run(), event.id().event()));
    std::normal_distribution<double> normal;
    edm4hep::RawTimeSeriesCollection output;
    RawTimeSeriesSimTrackerHitLinkCollection truthOutput;
    for (const auto &[cellID, channel] : channels) {
      const auto sample =
          response_.digitize(channel.depositedEnergyGeV, channel.layer,
                             channel.address.side, normal(engine));
      if (!sample.aboveSingleChannelThreshold) {
        continue;
      }

      auto digi = output.create();
      digi.setCellID(cellID);
      // VDSIM stores both signal and noise in quarter-ADC units. The signal is
      // the sole ADC datum; the quality byte preserves the separately packed
      // noise calibration until a dedicated VD raw-data type is introduced.
      digi.setQuality(sample.packedNoiseQuarterAdc);
      digi.setTime(static_cast<float>(channel.energyTimeGeVNs /
                                      channel.depositedEnergyGeV));
      digi.setCharge(static_cast<float>(sample.chargeFc));
      digi.setInterval(0.0F);
      digi.addToAdcCounts(sample.packedSignalQuarterAdc);

      for (const auto &[hitIndex, energy] : channel.depositedEnergyBySimHit) {
        auto link = truthOutput.create();
        link.setFrom(digi);
        link.setTo(simHits.at(hitIndex));
        link.setWeight(static_cast<float>(energy / channel.depositedEnergyGeV));
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
  const VertexModels models_;
  const simulation::VertexStripReadout stripReadout_;
  const simulation::VertexChannelResponse response_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiVertexDigitizerProducer);
