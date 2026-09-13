#pragma once

#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Simulation/InnerDetectorReadoutGeometry.h"

#include <array>
#include <cstdint>
#include <optional>

namespace delphi_edm4hep::simulation {

struct InnerDetectorJetCoordinate {
  double radiusCm{};
  double localPhiRadians{};
};

/// Native implementation of the ID jet-chamber SIFTOT phi-to-time response.
class InnerDetectorJetResponse {
public:
  static InnerDetectorJetResponse
  fromCargo(const geometry::CargoDatabase &database,
            const InnerDetectorReadoutGeometry &readout,
            double magneticFieldTesla);

  double driftTimeNs(std::uint32_t sector, std::uint32_t wire,
                     InnerDetectorDriftSide side, double localPhiRadians) const;
  std::optional<InnerDetectorJetCoordinate>
  coordinateFromDriftTime(std::uint32_t sector, std::uint32_t wire,
                          InnerDetectorDriftSide side,
                          double driftTimeNs) const;
  std::int32_t tdcCount(std::uint32_t sector, std::uint32_t wire,
                        double driftTimeNs) const;
  double driftTimeFromTdcCount(std::uint32_t sector, std::uint32_t wire,
                               std::int32_t count) const;
  double maximumDriftTimeNs() const;

  double magneticFieldTesla() const { return magneticFieldTesla_; }
  double lorentzAngleRadians() const { return lorentzAngleRadians_; }
  double boundaryAngleRadians() const { return boundaryAngleRadians_; }
  double boundaryHalfWidthCm() const { return boundaryHalfWidthCm_; }
  const std::array<double, 4> &velocityCorrections(std::uint32_t sector) const;

private:
  InnerDetectorReadoutGeometry readout_;
  std::array<std::array<double, 4>, 24> velocityCorrections_{};
  double magneticFieldTesla_{};
  double lorentzAngleRadians_{};
  double boundaryAngleRadians_{};
  double boundaryHalfWidthCm_{0.2};
};

} // namespace delphi_edm4hep::simulation
