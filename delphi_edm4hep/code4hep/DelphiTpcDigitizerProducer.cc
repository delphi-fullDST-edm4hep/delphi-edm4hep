#include "delphi_edm4hep/Code4hep/TpcDigiSimTrackerHitLinkCollection.h"
#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GeometryModel.h"
#include "delphi_edm4hep/Simulation/TpcDigitizationConditions.h"
#include "delphi_edm4hep/Simulation/TpcFadc.h"
#include "delphi_edm4hep/Simulation/TpcPadResponse.h"
#include "delphi_edm4hep/Simulation/TpcReadoutGeometry.h"
#include "delphi_edm4hep/Simulation/TpcTimeResponse.h"
#include "delphi_edm4hep/Simulation/TpcWireGeometry.h"
#include "delphi_edm4hep/Simulation/TpcWireResponse.h"

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
#include "edm4hep/TimeSeriesCollection.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace delphi_edm4hep {
namespace {

struct TpcModels {
  simulation::TpcReadoutGeometry readout;
  simulation::TpcDigitizationConditions conditions;
  simulation::TpcWireGeometry wires;
};

TpcModels readModels(const std::string &snapshot) {
  const auto database = geometry::CargoDatabase::readFile(snapshot);
  const auto geometry = geometry::GeometryModel::fromCargo(database, snapshot);
  auto readout = simulation::TpcReadoutGeometry::fromCargo(database, geometry);
  auto conditions =
      simulation::TpcDigitizationConditions::fromCargo(database, readout);
  auto wires = simulation::TpcWireGeometry::fromCargo(database, readout);
  return {std::move(readout), std::move(conditions), std::move(wires)};
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

template <typename Engine>
double truncatedNormal(Engine &engine, std::normal_distribution<double> &normal,
                       double limit) {
  for (;;) {
    const auto value = normal(engine);
    if (std::abs(value) <= limit) {
      return value;
    }
  }
}

struct PadWaveform {
  simulation::TpcPadAddress address;
  double phaseNormalDeviate{};
  std::vector<double> amplitudes;
  std::map<std::size_t, std::vector<double>> amplitudesBySimHit;
};

} // namespace

class DelphiTpcDigitizerProducer final : public edm::global::EDProducer<> {
public:
  explicit DelphiTpcDigitizerProducer(const edm::ParameterSet &config)
      : inputToken_(
            consumes(config.getParameter<edm::InputTag>("simTrackerHits"))),
        outputToken_(produces<edm4hep::TimeSeriesCollection>("TpcDigis")),
        truthOutputToken_(produces<TpcDigiSimTrackerHitLinkCollection>(
            "TpcDigiSimTrackerHitLinks")),
        randomSeed_(config.getParameter<unsigned int>("randomSeed")),
        electronEnergyEv_(config.getParameter<double>("electronEnergyEv")),
        avalancheScale_(config.getParameter<double>("avalancheScale")),
        magneticFieldTesla_(config.getParameter<double>("magneticFieldTesla")),
        models_(readModels(config.getParameter<std::string>("cargoSnapshot"))),
        padResponse_(models_.readout), wireResponse_(models_.wires) {
    if (randomSeed_ == 0 || electronEnergyEv_ <= 0 || avalancheScale_ <= 0 ||
        magneticFieldTesla_ < 0) {
      throw cms::Exception("Configuration")
          << "DelphiTpcDigitizerProducer requires positive seed and response "
             "parameters";
    }
  }

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("simTrackerHits");
    description.add<std::string>("cargoSnapshot");
    description.add<unsigned int>("randomSeed", 24680U);
    description.add<double>("electronEnergyEv", 20.0);
    description.add<double>("avalancheScale", 0.016);
    description.add<double>("magneticFieldTesla", 1.2312434);
    descriptions.addDefault(description);
  }

private:
  void produce(edm::StreamID, edm::Event &event,
               const edm::EventSetup &) const final {
    std::mt19937_64 engine(
        eventSeed(randomSeed_, event.id().run(), event.id().event()));
    std::normal_distribution<double> normal;
    std::map<unsigned int, double> rowPhases;
    std::map<std::uint64_t, PadWaveform> waveforms;

    const auto &simHits = event.get(inputToken_);
    for (std::size_t hitIndex = 0; hitIndex < simHits.size(); ++hitIndex) {
      const auto hit = simHits[hitIndex];
      if (hit.getEDep() <= 0) {
        continue;
      }
      const auto &position = hit.getPosition();
      const auto &momentum = hit.getMomentum();
      const auto momentumMagnitude =
          std::sqrt(momentum[0] * momentum[0] + momentum[1] * momentum[1] +
                    momentum[2] * momentum[2]);
      const auto halfPathMm = 0.5 * hit.getPathLength();
      const auto midpointXCm =
          (position[0] + (momentumMagnitude > 0
                              ? halfPathMm * momentum[0] / momentumMagnitude
                              : 0.0)) /
          10.0;
      const auto midpointYCm =
          (position[1] + (momentumMagnitude > 0
                              ? halfPathMm * momentum[1] / momentumMagnitude
                              : 0.0)) /
          10.0;
      const auto midpointZCm =
          (position[2] + (momentumMagnitude > 0
                              ? halfPathMm * momentum[2] / momentumMagnitude
                              : 0.0)) /
          10.0;
      const auto centralWire = models_.wires.locate(
          midpointXCm, midpointYCm, midpointZCm, momentum[0], momentum[1]);
      if (!centralWire) {
        continue;
      }
      const auto &sector = models_.conditions.sector(centralWire->sector);
      auto electrons = hit.getEDep() * 1.0e9 / electronEnergyEv_;
      if (sector.gateClosed) {
        electrons *= 0.85;
      }
      electrons = std::max(0.0, electrons + normal(engine) *
                                                std::sqrt(electrons * 0.19));
      const auto electronCount =
          static_cast<unsigned int>(std::lround(electrons));
      if (electronCount == 0) {
        continue;
      }
      const auto deltaZCm =
          momentumMagnitude > 0
              ? hit.getPathLength() / 10.0 * momentum[2] / momentumMagnitude
              : 0.0;
      const auto wireCharges = wireResponse_.distribute(
          midpointXCm, midpointYCm, midpointZCm, momentum[0], momentum[1],
          electronCount, magneticFieldTesla_,
          sector.driftVelocityCmPerMicrosecond,
          models_.conditions.highVoltageVolt(),
          models_.readout.driftHalfLengthCm());
      for (const auto &wireCharge : wireCharges) {
        const auto wireElectrons = static_cast<double>(wireCharge.electrons);
        const auto avalancheDraw = truncatedNormal(
            engine, normal, std::sqrt(std::max(0.0, wireElectrons * 1.5)));
        const auto signal =
            avalancheScale_ *
            std::max(0.0, wireElectrons +
                              avalancheDraw * std::sqrt(wireElectrons / 1.5));
        const auto induced = padResponse_.induce(
            wireCharge.positionCm[0], wireCharge.positionCm[1],
            wireCharge.positionCm[2], momentum[0], momentum[1], signal);
        for (const auto &padSignal : induced) {
          const auto cellId =
              simulation::TpcReadoutGeometry::encodeCellId(padSignal.address);
          const auto rowKey =
              padSignal.address.sector * 32U + padSignal.address.row;
          const auto [phase, inserted] = rowPhases.try_emplace(rowKey, 0.0);
          if (inserted) {
            phase->second = truncatedNormal(engine, normal, 5.0);
          }
          const auto sampled = timeResponse_.sample(
              wireCharge.positionCm[2], deltaZCm,
              models_.readout.driftHalfLengthCm(),
              sector.driftVelocityCmPerMicrosecond, padSignal.signal,
              phase->second, truncatedNormal(engine, normal, 4.0));
          auto [waveform, created] = waveforms.try_emplace(cellId);
          if (created) {
            waveform->second.address = padSignal.address;
            waveform->second.phaseNormalDeviate = phase->second;
            waveform->second.amplitudes.assign(400, 0.0);
          }
          auto [sourceAmplitudes, sourceCreated] =
              waveform->second.amplitudesBySimHit.try_emplace(hitIndex);
          if (sourceCreated) {
            sourceAmplitudes->second.assign(400, 0.0);
          }
          for (std::size_t index = 0; index < sampled.amplitudes.size();
               ++index) {
            const auto bin =
                sampled.firstBin + static_cast<unsigned int>(index);
            if (bin >= 1 && bin <= waveform->second.amplitudes.size()) {
              waveform->second.amplitudes[bin - 1] += sampled.amplitudes[index];
              sourceAmplitudes->second[bin - 1] += sampled.amplitudes[index];
            }
          }
        }
      }
    }

    edm4hep::TimeSeriesCollection output;
    TpcDigiSimTrackerHitLinkCollection truthOutput;
    for (const auto &[cellId, waveform] : waveforms) {
      const auto firstNonzero =
          std::find_if(waveform.amplitudes.begin(), waveform.amplitudes.end(),
                       [](double value) { return value > 0; });
      const auto lastNonzero =
          std::find_if(waveform.amplitudes.rbegin(), waveform.amplitudes.rend(),
                       [](double value) { return value > 0; });
      if (firstNonzero == waveform.amplitudes.end()) {
        continue;
      }
      const auto firstIndex = static_cast<std::size_t>(
          std::distance(waveform.amplitudes.begin(), firstNonzero));
      const auto lastIndex = waveform.amplitudes.size() - 1 -
                             static_cast<std::size_t>(std::distance(
                                 waveform.amplitudes.rbegin(), lastNonzero));
      const auto begin = firstIndex > 2 ? firstIndex - 2 : std::size_t{0};
      const auto end = std::min(waveform.amplitudes.size() - 1, lastIndex + 2);
      const std::vector<double> analog(waveform.amplitudes.begin() + begin,
                                       waveform.amplitudes.begin() + end + 1);
      const auto commonNoise = truncatedNormal(engine, normal, 4.0);
      std::vector<double> pixelNoise;
      pixelNoise.reserve(analog.size());
      for (std::size_t index = 0; index < analog.size(); ++index) {
        pixelNoise.push_back(truncatedNormal(engine, normal, 4.0));
      }
      const auto &calibration = models_.conditions.pad(
          waveform.address.sector, waveform.address.row, waveform.address.pad);
      const auto samples =
          fadc_.digitize(analog, calibration, commonNoise, pixelNoise);
      const auto clusters =
          fadc_.zeroSuppress(static_cast<unsigned int>(begin + 1), samples);
      const auto phaseTimeMicroseconds =
          timeResponse_.parameters().phaseSigmaBins *
          waveform.phaseNormalDeviate *
          timeResponse_.parameters().timeBinMicroseconds;
      for (const auto &cluster : clusters) {
        auto series = output.create();
        series.setCellID(cellId);
        series.setTime(static_cast<float>(
            1000.0 * (phaseTimeMicroseconds +
                      cluster.firstBin *
                          timeResponse_.parameters().timeBinMicroseconds)));
        series.setInterval(static_cast<float>(
            1000.0 * timeResponse_.parameters().timeBinMicroseconds));
        for (const auto sample : cluster.samples) {
          series.addToAmplitude(static_cast<float>(sample));
        }
        const auto clusterBegin =
            static_cast<std::size_t>(cluster.firstBin - 1);
        const auto clusterEnd = std::min(waveform.amplitudes.size(),
                                         clusterBegin + cluster.samples.size());
        std::vector<std::pair<std::size_t, double>> contributions;
        double totalContribution = 0.0;
        for (const auto &[hitIndex, sourceAmplitudes] :
             waveform.amplitudesBySimHit) {
          const auto contribution = std::max(
              0.0, std::accumulate(sourceAmplitudes.begin() + clusterBegin,
                                   sourceAmplitudes.begin() + clusterEnd, 0.0));
          contributions.emplace_back(hitIndex, contribution);
          totalContribution += contribution;
        }
        if (totalContribution <= 0.0) {
          totalContribution = 0.0;
          for (auto &[hitIndex, contribution] : contributions) {
            const auto &sourceAmplitudes =
                waveform.amplitudesBySimHit.at(hitIndex);
            contribution =
                std::max(0.0, std::accumulate(sourceAmplitudes.begin(),
                                              sourceAmplitudes.end(), 0.0));
            totalContribution += contribution;
          }
        }
        for (const auto &[hitIndex, contribution] : contributions) {
          if (contribution <= 0.0 || totalContribution <= 0.0) {
            continue;
          }
          auto link = truthOutput.create();
          link.setFrom(series);
          link.setTo(simHits.at(hitIndex));
          link.setWeight(static_cast<float>(contribution / totalContribution));
        }
      }
    }
    c4h::setCollectionID(output, event, *this, outputToken_);
    c4h::setCollectionID(truthOutput, event, *this, truthOutputToken_);
    event.emplace(outputToken_, std::move(output));
    event.emplace(truthOutputToken_, std::move(truthOutput));
  }

  const edm::EDGetTokenT<edm4hep::SimTrackerHitCollection> inputToken_;
  const edm::EDPutTokenT<edm4hep::TimeSeriesCollection> outputToken_;
  const edm::EDPutTokenT<TpcDigiSimTrackerHitLinkCollection> truthOutputToken_;
  const std::uint32_t randomSeed_;
  const double electronEnergyEv_;
  const double avalancheScale_;
  const double magneticFieldTesla_;
  const TpcModels models_;
  const simulation::TpcPadResponse padResponse_;
  const simulation::TpcWireResponse wireResponse_;
  const simulation::TpcTimeResponse timeResponse_;
  const simulation::TpcFadc fadc_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiTpcDigitizerProducer);
