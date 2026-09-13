#pragma once

#include "delphi_edm4hep/Simulation/VertexReadoutGeometry.h"

#include <array>
#include <cstdint>
#include <optional>

namespace delphi_edm4hep::simulation {

struct VertexStripAddress {
  std::uint32_t semanticSensor{};
  VertexReadoutSide side{};
  std::uint32_t strip{};
};

class VertexStripReadout {
public:
  explicit VertexStripReadout(VertexDigitizationConditions conditions =
                                  VertexDigitizationConditions::legacyV94c())
      : conditions_(conditions) {}

  std::optional<VertexStripAddress>
  locate(const VertexSensor &sensor, VertexReadoutSide side,
         const std::array<double, 3> &globalCm) const;
  std::array<double, 3> measurementCenter(const VertexSensor &sensor,
                                          VertexReadoutSide side,
                                          std::uint32_t strip) const;

  static std::uint64_t encodeCellID(const VertexStripAddress &address);
  static VertexStripAddress decodeCellID(std::uint64_t cellID);

private:
  VertexDigitizationConditions conditions_;
};

} // namespace delphi_edm4hep::simulation
