#pragma once

#include "delphi_edm4hep/Simulation/TpcWireGeometry.h"

#include <array>
#include <vector>

namespace delphi_edm4hep::simulation {

struct TpcWireResponseParameters {
  double diffusionZeroFieldCmPerSqrtCm{0.048};
  double leakageFactor{0.45};
};

struct TpcWireCharge {
  TpcWireAddress address;
  std::array<double, 3> positionCm{};
  unsigned int electrons{};
};

// Deterministic part of STDEDX/STLAND's wire response. The caller supplies
// the already fluctuated integer electron population; this class applies the
// field-dependent transverse diffusion and the measured leakage to the two
// adjacent wires. Avalanche fluctuations remain owned by the event module.
class TpcWireResponse {
public:
  explicit TpcWireResponse(
      TpcWireGeometry geometry,
      TpcWireResponseParameters parameters = TpcWireResponseParameters{});

  double transverseDiffusionCoefficient(double magneticFieldTesla,
                                        double driftVelocityCmPerMicrosecond,
                                        double highVoltageVolt,
                                        double driftHalfLengthCm) const;

  double transverseSigmaCm(double zCm, double magneticFieldTesla,
                           double driftVelocityCmPerMicrosecond,
                           double highVoltageVolt,
                           double driftHalfLengthCm) const;

  std::vector<TpcWireCharge>
  distribute(double xCm, double yCm, double zCm, double momentumX,
             double momentumY, unsigned int electrons,
             double magneticFieldTesla, double driftVelocityCmPerMicrosecond,
             double highVoltageVolt, double driftHalfLengthCm) const;

  const TpcWireGeometry &geometry() const { return geometry_; }
  const TpcWireResponseParameters &parameters() const { return parameters_; }

private:
  TpcWireGeometry geometry_;
  TpcWireResponseParameters parameters_;
};

} // namespace delphi_edm4hep::simulation
