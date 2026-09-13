#include "delphi_edm4hep/Simulation/TpcPadResponse.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace delphi_edm4hep::simulation {

TpcPadResponse::TpcPadResponse(TpcReadoutGeometry readout,
                               TpcPadResponseParameters parameters)
    : readout_(std::move(readout)), parameters_(parameters) {
  if (parameters_.padResponseSigmaCm <= 0 || parameters_.sigma0Cm2 <= 0 ||
      readout_.driftHalfLengthCm() <= 0) {
    throw std::runtime_error("invalid TPC pad-response parameters");
  }
}

std::vector<TpcInducedPadSignal>
TpcPadResponse::induce(double xCm, double yCm, double zCm,
                       double momentumX, double momentumY, double signal,
                       double rowToleranceCm) const {
  if (signal < 0) {
    throw std::runtime_error("TPC induced signal must be non-negative");
  }
  const auto central = readout_.locatePad(xCm, yCm, zCm, rowToleranceCm);
  if (!central || signal == 0) {
    return {};
  }

  const auto rowIt = std::find_if(
      readout_.rows().begin(), readout_.rows().end(),
      [&](const auto &row) { return row.number == central->row; });
  const auto sectorIt = std::find_if(
      readout_.sectors().begin(), readout_.sectors().end(),
      [&](const auto &sector) {
        return sector.readoutSector == central->sector &&
               sector.endcap == central->endcap;
      });
  if (rowIt == readout_.rows().end() ||
      sectorIt == readout_.sectors().end()) {
    throw std::runtime_error("TPC pad address is absent from readout geometry");
  }

  const auto angle = sectorIt->rotationDegrees * std::numbers::pi / 180.0;
  const auto dx = xCm - sectorIt->translationXCm;
  const auto dy = yCm - sectorIt->translationYCm;
  const auto localX = std::cos(angle) * dx + std::sin(angle) * dy;
  const auto localY = -std::sin(angle) * dx + std::cos(angle) * dy;
  const auto localMomentumX =
      std::cos(angle) * momentumX + std::sin(angle) * momentumY;
  const auto localMomentumY =
      -std::sin(angle) * momentumX + std::cos(angle) * momentumY;
  auto incidenceTangent = std::tan(std::atan2(localMomentumX, localMomentumY));
  if (zCm < 0) {
    incidenceTangent = -incidenceTangent;
  }
  const auto deltaPhi = (std::numbers::pi / 3.0) / rowIt->padCount;
  const auto padPitchCm = deltaPhi * rowIt->radiusCm;
  const auto driftCm = readout_.driftHalfLengthCm() - std::abs(zCm);
  if (driftCm < 0) {
    return {};
  }
  const auto cosine = std::cos(central->localPhiRadians);
  const auto sigmaSquared =
      parameters_.sigma0Cm2 +
      parameters_.padPitchSlopeCm *
          (padPitchCm - parameters_.padPitchReferenceCm) +
      parameters_.driftSlopeCm * driftCm *
          (1.0 + incidenceTangent * incidenceTangent) * cosine * cosine +
      parameters_.incidenceSlopeCm2 *
          std::pow((incidenceTangent - parameters_.lorentzTangent) * cosine,
                   2);
  if (sigmaSquared <= 0) {
    throw std::runtime_error("TPC STAMPA response variance is non-positive");
  }

  const auto sigmaPadSquared =
      2.0 * parameters_.padResponseSigmaCm *
      parameters_.padResponseSigmaCm;
  const auto normalization =
      0.5 * rowIt->padHeightCm * rowIt->padWidthCm /
      (std::numbers::pi * sigmaPadSquared);
  const auto half = static_cast<int>(rowIt->padCount / 2);
  const auto first = std::max(1, static_cast<int>(central->pad) - 2);
  const auto last = std::min(static_cast<int>(rowIt->padCount),
                             static_cast<int>(central->pad) + 2);

  std::vector<TpcInducedPadSignal> result;
  result.reserve(static_cast<std::size_t>(last - first + 1));
  for (auto pad = first; pad <= last; ++pad) {
    const auto padPhi = (pad - half) * deltaPhi - 0.5 * deltaPhi;
    const auto padX = rowIt->radiusCm * std::sin(padPhi);
    const auto padY = rowIt->radiusCm * std::cos(padPhi);
    const auto distanceSquared =
        (padX - localX) * (padX - localX) +
        (padY - localY) * (padY - localY);
    const auto response =
        normalization * std::exp(-distanceSquared / (2.0 * sigmaSquared));
    auto address = *central;
    address.pad = static_cast<unsigned int>(pad);
    result.push_back({address, response, signal * response});
  }
  return result;
}

} // namespace delphi_edm4hep::simulation
