#pragma once

#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GeometryModel.h"
#include "delphi_edm4hep/Simulation/VertexDigitizationConditions.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace delphi_edm4hep::simulation {

struct VertexRigidTransform {
  std::array<double, 3> translationCm{};
  // Row-major local-to-DELPHI rotation matrix, as stored by DBF MTRX.
  std::array<double, 9> rotation{};

  std::array<double, 3>
  localToGlobal(const std::array<double, 3> &localCm) const;
  std::array<double, 3>
  globalToLocal(const std::array<double, 3> &globalCm) const;
};

struct VertexActiveLine {
  std::array<double, 3> firstCm{};
  std::array<double, 3> secondCm{};

  double lengthCm() const;
};

struct VertexSensor {
  std::uint32_t semanticSensor{};
  std::uint64_t cellIDBase{};
  std::string path;
  VertexBarrelLayer layer{};
  std::uint32_t module{};
  std::uint32_t physicalPlaquette{};
  int halfModuleSign{};
  VertexLongitudinalRegion longitudinalRegion{};
  std::array<double, 3> dimensionsCm{};
  VertexPlaquetteConditions readout;
  VertexRigidTransform pTransform;
  VertexActiveLine pActiveLine;
  std::optional<VertexRigidTransform> nTransform;
  std::optional<VertexActiveLine> nActiveLine;
};

struct VertexElectronicsAddress {
  std::uint32_t sirocco{};
  std::uint32_t channel{};
};

class VertexReadoutGeometry {
public:
  static constexpr std::uint8_t subsystem = 1;

  static VertexReadoutGeometry
  fromCargo(const geometry::CargoDatabase &database,
            const geometry::GeometryModel &geometry,
            const VertexDigitizationConditions &conditions =
                VertexDigitizationConditions::legacyV94c());

  const std::vector<VertexSensor> &sensors() const { return sensors_; }
  const VertexSensor &sensor(std::uint32_t semanticSensor) const;
  const VertexSensor &sensorForTransportCellID(std::uint64_t cellID) const;
  VertexElectronicsAddress electronicsAddress(const VertexSensor &sensor,
                                              VertexReadoutSide side,
                                              std::uint32_t readoutStrip) const;

  static std::uint64_t cellIDBase(std::uint32_t semanticSensor);
  static std::uint32_t semanticSensor(std::uint64_t transportCellID);

private:
  explicit VertexReadoutGeometry(std::vector<VertexSensor> sensors);
  std::vector<VertexSensor> sensors_;
};

} // namespace delphi_edm4hep::simulation
