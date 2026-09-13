#pragma once

#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Simulation/TpcReadoutGeometry.h"

#include <array>
#include <optional>

namespace delphi_edm4hep::simulation {

struct TpcWireAddress {
  unsigned int endcap{};
  unsigned int sector{};
  unsigned int wire{};
  int outwardDirection{};
  double localXCm{};
  double localZCm{};
};

// Sense-wire geometry decoded from the WIRE SENS record used by STINI.
// firstWireRadiusCm is BOUNDI in the legacy source; wireReferenceCm is
// DWZTPC, the lower edge from which STINTR determines the wire number.
class TpcWireGeometry {
public:
  TpcWireGeometry(TpcReadoutGeometry readout, unsigned int wireCount,
                  double wireSpacingCm, double firstWireRadiusCm,
                  unsigned int highWireStart, double highWireHalfWidthCm,
                  double highWireHalfWidthSlopeCm);

  static TpcWireGeometry fromCargo(const geometry::CargoDatabase &database,
                                   const TpcReadoutGeometry &readout);

  unsigned int wireCount() const { return wireCount_; }
  double wireSpacingCm() const { return wireSpacingCm_; }
  double firstWireRadiusCm() const { return firstWireRadiusCm_; }
  double wireReferenceCm() const { return wireReferenceCm_; }
  unsigned int highWireStart() const { return highWireStart_; }
  double highWireHalfWidthCm() const { return highWireHalfWidthCm_; }
  double highWireHalfWidthSlopeCm() const { return highWireHalfWidthSlopeCm_; }

  double wireRadiusCm(unsigned int wire) const;
  double maximumAbsLocalXCm(unsigned int wire) const;

  // Match STINTR: select the closest sector, apply its 30-degree live wedge,
  // assign the radial wire cell, reject the tapered high-wire dead area, and
  // retain the sign of the radial track direction.
  std::optional<TpcWireAddress> locate(double xCm, double yCm, double zCm,
                                       double momentumX,
                                       double momentumY) const;

  std::array<double, 3> wirePoint(const TpcWireAddress &address) const;

private:
  TpcReadoutGeometry readout_;
  unsigned int wireCount_{};
  double wireSpacingCm_{};
  double firstWireRadiusCm_{};
  double wireReferenceCm_{};
  unsigned int highWireStart_{};
  double highWireHalfWidthCm_{};
  double highWireHalfWidthSlopeCm_{};
};

} // namespace delphi_edm4hep::simulation
