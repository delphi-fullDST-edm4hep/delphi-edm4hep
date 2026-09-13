#pragma once

#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Simulation/TpcReadoutGeometry.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace delphi_edm4hep::simulation {

struct TpcSectorConditions {
  unsigned int readoutSector{};
  unsigned int geometrySector{};
  unsigned int endcap{};
  double driftVelocityCmPerMicrosecond{};
  bool gateClosed{};
};

struct TpcPadElectronicsCalibration {
  unsigned int readoutSector{};
  unsigned int geometrySector{};
  unsigned int endcap{};
  unsigned int row{};
  unsigned int pad{};
  unsigned int electronicsChannel{};
  unsigned int status{};
  double pedestalCounts{};
  double lowRangeSignalPerCount{};
  double highRangeSignalPerCount{};
  double highRangePedestalCounts{};
  double gainRatio{};
  double rangeBreakSignal{};
};

class TpcDigitizationConditions {
public:
  static TpcDigitizationConditions
  fromCargo(const geometry::CargoDatabase &database,
            const TpcReadoutGeometry &readout);

  double highVoltageVolt() const { return highVoltageVolt_; }
  double minimumIonizingDedx() const { return minimumIonizingDedx_; }
  double meanPadAmplitude() const { return meanPadAmplitude_; }
  const std::vector<TpcSectorConditions> &sectors() const { return sectors_; }
  const std::vector<TpcPadElectronicsCalibration> &pads() const {
    return pads_;
  }

  const TpcSectorConditions &sector(unsigned int readoutSector) const;
  const TpcPadElectronicsCalibration &pad(unsigned int readoutSector,
                                          unsigned int row,
                                          unsigned int pad) const;

private:
  double highVoltageVolt_{};
  double minimumIonizingDedx_{};
  double meanPadAmplitude_{};
  std::vector<TpcSectorConditions> sectors_;
  std::vector<TpcPadElectronicsCalibration> pads_;
  std::unordered_map<std::uint64_t, std::size_t> padIndices_;
};

} // namespace delphi_edm4hep::simulation
