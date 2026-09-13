#pragma once

#include "delphi_edm4hep/Simulation/TpcReadoutGeometry.h"

#include <vector>

namespace delphi_edm4hep::simulation {

// Parameters from STSPRF and STINI in SIMANA v94c's tpcsim.car. Their
// names and grouping intentionally follow the STAMPA response calculation.
struct TpcPadResponseParameters {
  double padResponseSigmaCm{0.37};
  double padPitchReferenceCm{0.597};
  double sigma0Cm2{0.1064};
  double padPitchSlopeCm{0.1495};
  double driftSlopeCm{0.91e-4};
  double incidenceSlopeCm2{0.00782};
  double lorentzTangent{-0.6613};
};

struct TpcInducedPadSignal {
  TpcPadAddress address;
  double response{};
  double signal{};
};

class TpcPadResponse {
public:
  explicit TpcPadResponse(
      TpcReadoutGeometry readout,
      TpcPadResponseParameters parameters = TpcPadResponseParameters{});

  // Reproduce STAMPA's deterministic five-pad induction kernel. signal is in
  // caller-defined units: STAMPA supplied the fluctuated wire charge, while a
  // future Code4hep ionisation model will supply the native equivalent.
  std::vector<TpcInducedPadSignal>
  induce(double xCm, double yCm, double zCm, double momentumX,
         double momentumY, double signal,
         double rowToleranceCm = 1.0) const;

  const TpcPadResponseParameters &parameters() const { return parameters_; }

private:
  TpcReadoutGeometry readout_;
  TpcPadResponseParameters parameters_;
};

} // namespace delphi_edm4hep::simulation
