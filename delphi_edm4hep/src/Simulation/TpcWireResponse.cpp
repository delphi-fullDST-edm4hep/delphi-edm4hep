#include "delphi_edm4hep/Simulation/TpcWireResponse.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace delphi_edm4hep::simulation {

TpcWireResponse::TpcWireResponse(TpcWireGeometry geometry,
                                 TpcWireResponseParameters parameters)
    : geometry_(std::move(geometry)), parameters_(parameters) {
  if (parameters_.diffusionZeroFieldCmPerSqrtCm <= 0 ||
      parameters_.leakageFactor < 0) {
    throw std::runtime_error("invalid TPC wire-response parameters");
  }
}

double TpcWireResponse::transverseDiffusionCoefficient(
    double magneticFieldTesla, double driftVelocityCmPerMicrosecond,
    double highVoltageVolt, double driftHalfLengthCm) const {
  if (magneticFieldTesla < 0 || driftVelocityCmPerMicrosecond <= 0 ||
      highVoltageVolt <= 0 || driftHalfLengthCm <= 0) {
    throw std::runtime_error("invalid TPC transverse-diffusion conditions");
  }
  // STINI: SIGTD = 0.048 / SQRT(1+(BFIELD*TPCDRV*100/EFIELD)**2),
  // with EFIELD=TPCHHV/ZMAXTP and BFIELD in tesla.
  const auto electricFieldVoltPerCm = highVoltageVolt / driftHalfLengthCm;
  const auto magneticSuppression = magneticFieldTesla *
                                   driftVelocityCmPerMicrosecond * 100.0 /
                                   electricFieldVoltPerCm;
  return parameters_.diffusionZeroFieldCmPerSqrtCm /
         std::sqrt(1.0 + magneticSuppression * magneticSuppression);
}

double TpcWireResponse::transverseSigmaCm(double zCm, double magneticFieldTesla,
                                          double driftVelocityCmPerMicrosecond,
                                          double highVoltageVolt,
                                          double driftHalfLengthCm) const {
  const auto driftCm = driftHalfLengthCm - std::abs(zCm);
  if (driftCm <= 0) {
    return 0;
  }
  return std::sqrt(driftCm) *
         transverseDiffusionCoefficient(magneticFieldTesla,
                                        driftVelocityCmPerMicrosecond,
                                        highVoltageVolt, driftHalfLengthCm);
}

std::vector<TpcWireCharge> TpcWireResponse::distribute(
    double xCm, double yCm, double zCm, double momentumX, double momentumY,
    unsigned int electrons, double magneticFieldTesla,
    double driftVelocityCmPerMicrosecond, double highVoltageVolt,
    double driftHalfLengthCm) const {
  if (electrons == 0) {
    return {};
  }
  const auto central = geometry_.locate(xCm, yCm, zCm, momentumX, momentumY);
  if (!central) {
    return {};
  }
  const auto sigma =
      transverseSigmaCm(zCm, magneticFieldTesla, driftVelocityCmPerMicrosecond,
                        highVoltageVolt, driftHalfLengthCm);
  const auto escaped = std::min<unsigned int>(
      electrons / 2,
      static_cast<unsigned int>(parameters_.leakageFactor * electrons * sigma));
  const auto centralElectrons = electrons - 2 * escaped;

  std::vector<TpcWireCharge> result;
  result.reserve(3);
  for (int offset = -1; offset <= 1; ++offset) {
    const auto population = offset == 0 ? centralElectrons : escaped;
    if (population == 0) {
      continue;
    }
    auto address = *central;
    const auto wire =
        static_cast<int>(central->wire) + offset * central->outwardDirection;
    if (wire < 1 || wire > static_cast<int>(geometry_.wireCount())) {
      continue;
    }
    address.wire = static_cast<unsigned int>(wire);
    const auto position = geometry_.wirePoint(address);
    // STSIM rejects leaked hits which land in the tapered high-wire dead area.
    if (std::abs(address.localXCm) >
        geometry_.maximumAbsLocalXCm(address.wire)) {
      continue;
    }
    result.push_back({address, position, population});
  }
  return result;
}

} // namespace delphi_edm4hep::simulation
