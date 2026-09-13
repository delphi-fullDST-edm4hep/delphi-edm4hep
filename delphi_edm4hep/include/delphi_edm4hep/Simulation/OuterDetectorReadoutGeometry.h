#pragma once

#include "delphi_edm4hep/Geometry/CargoDatabase.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace delphi_edm4hep::simulation {

enum class OuterDetectorDriftSide : std::uint8_t { Negative = 0, Positive = 1 };

struct OuterDetectorChannel {
  std::uint32_t plank{};
  std::uint32_t layer{};
  std::uint32_t column{};
};

struct OuterDetectorAddress {
  std::uint32_t plank{};
  std::uint32_t layer{};
  std::uint32_t column{};
  OuterDetectorDriftSide side{};
};

struct OuterDetectorTube {
  OuterDetectorChannel channel;
  double wireXAtEndCm{};
  double wireYAtEndCm{};
  double zNegativeCm{};
  double zPositiveCm{};
  double pedestalNs{};
  double zPropagationNs{};
  double pulseWidthNs{};
  double efficiency{};
  bool active{};
};

struct OuterDetectorLocatedTube {
  const OuterDetectorTube *tube{};
  double wireXCm{};
  double wireYCm{};
  double localXCm{};
  double localYCm{};
  double distanceCm{};
  double angleRadians{};
};

class OuterDetectorReadoutGeometry {
public:
  static OuterDetectorReadoutGeometry
  fromCargo(const geometry::CargoDatabase &database);

  const std::vector<OuterDetectorTube> &tubes() const { return tubes_; }
  const OuterDetectorTube &tube(const OuterDetectorChannel &channel) const;
  std::optional<OuterDetectorLocatedTube> locate(double xCm, double yCm,
                                                  double zCm) const;
  std::array<double, 2> wirePosition(const OuterDetectorTube &tube,
                                     double zCm) const;
  std::array<double, 2> localToGlobalDirection(std::uint32_t plank,
                                               double localX,
                                               double localY) const;

  double cellPitchXCm() const { return cellPitchXCm_; }
  double layerPitchYCm() const { return layerPitchYCm_; }
  double gasWidthCm() const { return gasWidthCm_; }
  double gasHeightCm() const { return gasHeightCm_; }
  double transverseResolutionCm() const { return 0.010; }
  double longitudinalResolutionCm() const { return 5.49; }

  static std::uint64_t encodeChannelID(const OuterDetectorChannel &channel);
  static OuterDetectorChannel decodeChannelID(std::uint64_t cellID);
  static std::uint64_t encodeCellID(const OuterDetectorAddress &address);
  static OuterDetectorAddress decodeCellID(std::uint64_t cellID);

private:
  std::vector<OuterDetectorTube> tubes_;
  std::array<double, 24> plankAngles_{};
  double cellPitchXCm_{};
  double layerPitchYCm_{};
  double gasWidthCm_{1.645};
  double gasHeightCm_{1.645};
};

} // namespace delphi_edm4hep::simulation
