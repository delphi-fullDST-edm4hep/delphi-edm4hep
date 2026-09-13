#pragma once

#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GeometryModel.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace delphi_edm4hep::simulation {

struct TpcPadRow {
  unsigned int number{};
  unsigned int padCount{};
  double radiusCm{};
  double padHeightCm{};
  double padWidthCm{};
};

struct TpcSectorTransform {
  unsigned int readoutSector{};
  unsigned int geometrySector{};
  unsigned int endcap{};
  double translationXCm{};
  double translationYCm{};
  double rotationDegrees{};
};

struct TpcPadAddress {
  unsigned int endcap{};
  unsigned int sector{};
  unsigned int row{};
  unsigned int pad{};
  double radialResidualCm{};
  double localPhiRadians{};
};

class TpcReadoutGeometry {
public:
  TpcReadoutGeometry(std::vector<TpcPadRow> rows,
                     std::vector<TpcSectorTransform> sectors,
                     double driftHalfLengthCm);

  static TpcReadoutGeometry fromCargo(const geometry::CargoDatabase &database,
                                      const geometry::GeometryModel &geometry);

  const std::vector<TpcPadRow> &rows() const { return rows_; }
  const std::vector<TpcSectorTransform> &sectors() const { return sectors_; }
  double driftHalfLengthCm() const { return driftHalfLengthCm_; }

  std::optional<TpcPadAddress> locatePad(double xCm, double yCm, double zCm,
                                         double rowToleranceCm = 1.0) const;
  std::array<double, 3> padCenter(const TpcPadAddress &address,
                                  double zCm) const;
  static std::uint64_t encodeCellId(const TpcPadAddress &address);
  static TpcPadAddress decodeCellId(std::uint64_t cellId);

private:
  std::vector<TpcPadRow> rows_;
  std::vector<TpcSectorTransform> sectors_;
  double driftHalfLengthCm_{};
};

} // namespace delphi_edm4hep::simulation
