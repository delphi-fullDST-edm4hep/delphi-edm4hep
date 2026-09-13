#pragma once

#include "delphi_edm4hep/Geometry/CargoDatabase.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace delphi_edm4hep::simulation {

struct InnerDetectorJetWire {
  std::uint32_t wire{};
  double radiusCm{};
  std::array<double, 21> calibration{};
  std::int32_t status{};
};

struct InnerDetectorJetSector {
  std::uint32_t sector{};
  double halfLengthCm{};
  std::array<InnerDetectorJetWire, 24> wires;
};

struct InnerDetectorTriggerChannel {
  std::uint32_t channel{};
  std::array<double, 2> calibration{};
  std::int32_t status{};
};

struct InnerDetectorTriggerLayer {
  std::uint32_t layer{};
  double halfLengthCm{};
  double anodeRadiusCm{};
  double anodeFirstPhiRadians{};
  double anodePitchRadians{};
  double cathodeRadiusCm{};
  double cathodeFirstZCm{};
  double cathodeWidthCm{};
  std::array<InnerDetectorTriggerChannel, 192> anodes;
  std::array<InnerDetectorTriggerChannel, 192> cathodes;
};

enum class InnerDetectorTriggerSide : std::uint8_t { Anode = 0, Cathode = 1 };
enum class InnerDetectorDriftSide : std::uint8_t { Left = 0, Right = 1 };

struct InnerDetectorJetAddress {
  std::uint32_t sector{};
  std::uint32_t wire{};
  InnerDetectorDriftSide side{};
  double localPhiRadians{};
};

struct InnerDetectorJetChannel {
  std::uint32_t sector{};
  std::uint32_t wire{};
};

struct InnerDetectorJetCrossing {
  InnerDetectorJetAddress address;
  std::array<double, 3> positionCm{};
  double pathFraction{};
};

struct InnerDetectorTriggerAddress {
  std::uint32_t layer{};
  InnerDetectorTriggerSide side{};
  std::uint32_t channel{};
};

class InnerDetectorReadoutGeometry {
public:
  static InnerDetectorReadoutGeometry
  fromCargo(const geometry::CargoDatabase &database);

  const std::array<InnerDetectorJetSector, 24> &jetSectors() const {
    return jetSectors_;
  }
  const std::array<InnerDetectorTriggerLayer, 5> &triggerLayers() const {
    return triggerLayers_;
  }

  double driftTimeZeroNs() const { return driftTimeZeroNs_; }
  double cathodeTimeZeroNs() const { return cathodeTimeZeroNs_; }
  double bunchTimeZeroNs() const { return bunchTimeZeroNs_; }
  double deadTimeMicroseconds() const { return deadTimeMicroseconds_; }
  double cathodeToAnodeRatio() const { return cathodeToAnodeRatio_; }
  double cathodeDistributionSigmaCm() const {
    return cathodeDistributionSigmaCm_;
  }

  std::optional<InnerDetectorTriggerAddress>
  locateAnode(std::uint32_t layer, double xCm, double yCm) const;
  std::optional<InnerDetectorTriggerAddress> locateCathode(std::uint32_t layer,
                                                           double zCm) const;
  double anodePhi(std::uint32_t layer, std::uint32_t wire) const;
  double cathodeZ(std::uint32_t layer, std::uint32_t strip) const;
  std::optional<InnerDetectorJetAddress> locateJet(double xCm, double yCm,
                                                   double zCm) const;
  std::vector<InnerDetectorJetCrossing>
  jetWireCrossings(const std::array<double, 3> &startCm,
                   const std::array<double, 3> &endCm) const;
  double jetSectorMidPhi(std::uint32_t sector) const;
  static std::uint64_t
  encodeJetChannelID(const InnerDetectorJetChannel &channel);
  static InnerDetectorJetChannel decodeJetChannelID(std::uint64_t cellID);
  static std::uint64_t encodeJetCellID(const InnerDetectorJetAddress &address);
  static InnerDetectorJetAddress decodeJetCellID(std::uint64_t cellID);

private:
  std::array<InnerDetectorJetSector, 24> jetSectors_;
  std::array<InnerDetectorTriggerLayer, 5> triggerLayers_;
  double driftTimeZeroNs_{};
  double cathodeTimeZeroNs_{};
  double bunchTimeZeroNs_{};
  double deadTimeMicroseconds_{0.055};
  double cathodeToAnodeRatio_{2.3875};
  double cathodeDistributionSigmaCm_{0.286};
  double jetFirstSectorMidPhiRadians_{};
};

} // namespace delphi_edm4hep::simulation
