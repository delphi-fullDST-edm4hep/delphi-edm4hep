#include "delphi_edm4hep/Reconstruction/CentralTrackFit.h"
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
#include "edm4hep/MutableTrack.h"
#include "edm4hep/TrackCollection.h"
#include "edm4hep/TrackState.h"

#include "Code4hep/PodioUtilities/setCollectionID.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace delphi_edm4hep {
namespace {

struct Accumulator {
  double x{};
  double y{};
  double z{};
  std::size_t count{};
};

void copyRelations(edm4hep::MutableTrack destination,
                   const edm4hep::Track &source) {
  for (const auto &hit : source.getTrackerHits()) {
    destination.addToTrackerHits(hit);
  }
  for (const auto &segment : source.getTracks()) {
    destination.addToTracks(segment);
  }
  for (const auto number : source.getSubdetectorHitNumbers()) {
    destination.addToSubdetectorHitNumbers(number);
  }
  for (const auto number : source.getSubdetectorHoleNumbers()) {
    destination.addToSubdetectorHoleNumbers(number);
  }
}

void copyTrackUnchanged(edm4hep::MutableTrack destination,
                        const edm4hep::Track &source) {
  destination.setType(source.getType());
  destination.setChi2(source.getChi2());
  destination.setNdf(source.getNdf());
  destination.setNholes(source.getNholes());
  copyRelations(destination, source);
  for (const auto &state : source.getTrackStates()) {
    destination.addToTrackStates(state);
  }
}

} // namespace

class DelphiCentralTrackRefitProducer final
    : public edm::global::EDProducer<> {
public:
  explicit DelphiCentralTrackRefitProducer(const edm::ParameterSet &config)
      : inputToken_(consumes(config.getParameter<edm::InputTag>("tracks"))),
        outputToken_(
            produces<edm4hep::TrackCollection>("RefittedCentralTracks")),
        vertexTransverseSigmaMm_(
            config.getParameter<double>("vertexTransverseSigmaMm")),
        innerDetectorTransverseSigmaMm_(config.getParameter<double>(
            "innerDetectorTransverseSigmaMm")),
        tpcTransverseSigmaMm_(
            config.getParameter<double>("tpcTransverseSigmaMm")),
        tpcLongitudinalSigmaMm_(
            config.getParameter<double>("tpcLongitudinalSigmaMm")),
        outerDetectorTransverseSigmaMm_(config.getParameter<double>(
            "outerDetectorTransverseSigmaMm")),
        outerDetectorLongitudinalSigmaMm_(config.getParameter<double>(
            "outerDetectorLongitudinalSigmaMm")),
        constrainToInteractionPoint_(
            config.getParameter<bool>("constrainToInteractionPoint")) {
    for (const auto sigma :
         {vertexTransverseSigmaMm_, innerDetectorTransverseSigmaMm_,
          tpcTransverseSigmaMm_, tpcLongitudinalSigmaMm_,
          outerDetectorTransverseSigmaMm_,
          outerDetectorLongitudinalSigmaMm_}) {
      if (!std::isfinite(sigma) || sigma <= 0.0) {
        throw std::invalid_argument(
            "central track refit resolutions must all be positive");
      }
    }
  }

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription description;
    description.add<edm::InputTag>("tracks");
    description.add<double>("vertexTransverseSigmaMm", 1.0);
    description.add<double>("innerDetectorTransverseSigmaMm", 1.0);
    description.add<double>("tpcTransverseSigmaMm", 5.0);
    description.add<double>("tpcLongitudinalSigmaMm", 10.0);
    description.add<double>("outerDetectorTransverseSigmaMm", 10.0);
    description.add<double>("outerDetectorLongitudinalSigmaMm", 55.0);
    description.add<bool>("constrainToInteractionPoint", true);
    descriptions.addDefault(description);
  }

private:
  void produce(edm::StreamID, edm::Event &event,
               const edm::EventSetup &) const final {
    edm4hep::TrackCollection output;
    for (const auto source : event.get(inputToken_)) {
      std::vector<reconstruction::SpacePointMeasurement> measurements;
      std::map<std::tuple<unsigned int, unsigned int, unsigned int>,
               Accumulator>
          tpcRows;
      for (const auto &hit : source.getTrackerHits()) {
        const auto cellID = hit.getCellID();
        const auto subsystem = static_cast<unsigned int>(cellID >> 56U);
        const auto position = hit.getPosition();
        if (subsystem == 0U) {
          const auto address =
              simulation::TpcReadoutGeometry::decodeCellId(cellID);
          auto &row = tpcRows[{address.endcap, address.sector, address.row}];
          row.x += position[0];
          row.y += position[1];
          row.z += position[2];
          ++row.count;
        } else if (subsystem == 1U) {
          measurements.push_back({{position[0], position[1], position[2]},
                                  vertexTransverseSigmaMm_, std::nullopt});
        } else if (subsystem == 2U) {
          measurements.push_back(
              {{position[0], position[1], position[2]},
               innerDetectorTransverseSigmaMm_, std::nullopt});
        } else if (subsystem == 4U) {
          measurements.push_back(
              {{position[0], position[1], position[2]},
               outerDetectorTransverseSigmaMm_,
               outerDetectorLongitudinalSigmaMm_});
        }
      }
      for (const auto &[address, row] : tpcRows) {
        static_cast<void>(address);
        measurements.push_back(
            {{row.x / row.count, row.y / row.count, row.z / row.count},
             tpcTransverseSigmaMm_, tpcLongitudinalSigmaMm_});
      }

      const auto fit = reconstruction::fitCentralTrackMeasurements(
          measurements, constrainToInteractionPoint_);
      auto destination = output.create();
      if (!fit) {
        copyTrackUnchanged(destination, source);
        continue;
      }

      destination.setType(source.getType());
      destination.setChi2(static_cast<float>(fit->chi2));
      destination.setNdf(fit->ndf);
      destination.setNholes(source.getNholes());
      copyRelations(destination, source);

      edm4hep::TrackState state{};
      state.location = edm4hep::TrackState::AtIP;
      state.D0 = static_cast<float>(fit->d0Mm);
      state.phi = static_cast<float>(fit->phiRadians);
      state.omega = static_cast<float>(fit->omegaPerMm);
      state.Z0 = static_cast<float>(fit->z0Mm);
      state.tanLambda = static_cast<float>(fit->tanLambda);
      state.time = 0.0F;
      state.referencePoint = {0.0F, 0.0F, 0.0F};
      using P = edm4hep::TrackParams;
      auto minimumRadius = std::numeric_limits<double>::infinity();
      double maximumRadius{};
      for (const auto &measurement : measurements) {
        const auto radius = std::hypot(measurement.position.xMm,
                                       measurement.position.yMm);
        minimumRadius = std::min(minimumRadius, radius);
        maximumRadius = std::max(maximumRadius, radius);
      }
      const auto radialSpan = std::max(1.0, maximumRadius - minimumRadius);
      state.covMatrix.setValue(
          vertexTransverseSigmaMm_ * vertexTransverseSigmaMm_, P::d0,
          P::d0);
      state.covMatrix.setValue(
          std::pow(tpcTransverseSigmaMm_ / radialSpan, 2), P::phi, P::phi);
      state.covMatrix.setValue(
          std::pow(tpcTransverseSigmaMm_ / (radialSpan * radialSpan), 2),
          P::omega, P::omega);
      state.covMatrix.setValue(
          tpcLongitudinalSigmaMm_ * tpcLongitudinalSigmaMm_, P::z0,
          P::z0);
      state.covMatrix.setValue(
          std::pow(tpcLongitudinalSigmaMm_ / radialSpan, 2), P::tanLambda,
          P::tanLambda);
      destination.addToTrackStates(state);
    }
    c4h::setCollectionID(output, event, *this, outputToken_);
    event.emplace(outputToken_, std::move(output));
  }

  const edm::EDGetTokenT<edm4hep::TrackCollection> inputToken_;
  const edm::EDPutTokenT<edm4hep::TrackCollection> outputToken_;
  const double vertexTransverseSigmaMm_;
  const double innerDetectorTransverseSigmaMm_;
  const double tpcTransverseSigmaMm_;
  const double tpcLongitudinalSigmaMm_;
  const double outerDetectorTransverseSigmaMm_;
  const double outerDetectorLongitudinalSigmaMm_;
  const bool constrainToInteractionPoint_;
};

} // namespace delphi_edm4hep

DEFINE_FWK_MODULE(delphi_edm4hep::DelphiCentralTrackRefitProducer);
