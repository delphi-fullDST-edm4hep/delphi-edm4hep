#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Simulation/TpcDigitizationConditions.h"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool close(double left, double right, double tolerance = 1e-12) {
  return std::abs(left - right) <= tolerance;
}

} // namespace

int main() {
  std::istringstream input(R"(*CALB /TPC*.B
940624,235900,941030,222515
*ISLO 16,0,0,0,0,0,0,11184810,0,0,0,0,0,0,0,0,0
*SLOW 18,0,0,0,975,986.2,25.306,-189,-169,91.7,12.5,19.1,9.1,12.5,1.03005,975.21,989.93,-169,-154.5
*USER 16,254.5,.959,.942,1.005,1.007,.978,1.048,.977,1.042,.981,.989,1.037,1.036,652.8,1,1.066
**
*CALB /TPC*/ENP0.B
940624,235900,941030,231638
*SLOW 14,6.998,0,0,0,0,0,0,0,0,0,0,0,0,0
**
*CALB /TPC*/ENP1.B
940624,235900,941030,232213
*SLOW 14,7.002,0,0,0,0,0,0,0,0,0,0,0,0,0
**
*CALB /TPC*/ENP0/SC00.SENS$PR01.B
940624,235900,940802,144001
*CALP 4,393217000,327680000,393217100,32374784
*LEAD 12,2,0,0,0,0,0,0,1000,0,0,0,0
*STAT 2,1,152007
**
*CALB /TPC*/ENP1/SC01.SENS$PR01.B
940624,235900,940802,144001
*CALP 4,393217000,327680000,393217100,32374784
*LEAD 12,2,0,0,0,0,0,0,1000,0,0,0,0
*STAT 1,0
**
)");
  const auto database =
      delphi_edm4hep::geometry::CargoDatabase::read(input, "fixture");
  const delphi_edm4hep::simulation::TpcReadoutGeometry readout(
      {{1, 2, 36.5, .959, .547}},
      {{1, 0, 0, 0, 0, 0}, {12, 1, 1, 0, 0, 0}}, 145.0);
  const auto conditions =
      delphi_edm4hep::simulation::TpcDigitizationConditions::fromCargo(
          database, readout);
  require(conditions.highVoltageVolt() == 25306.0, "wrong high voltage");
  require(conditions.minimumIonizingDedx() == 254.5,
          "wrong dE/dx normalization");
  require(conditions.meanPadAmplitude() == 652.8,
          "wrong mean pad amplitude");
  require(conditions.sector(1).driftVelocityCmPerMicrosecond == 6.998,
          "wrong negative-endcap drift velocity");
  require(conditions.sector(12).driftVelocityCmPerMicrosecond == 7.002,
          "wrong positive-endcap drift velocity");
  require(conditions.sector(1).gateClosed && conditions.sector(12).gateClosed,
          "wrong packed gate-state decoding");
  require(conditions.pads().size() == 4, "wrong pad-calibration count");
  const auto &pad = conditions.pad(1, 1, 1);
  require(pad.electronicsChannel == 1520, "wrong electronics channel map");
  require(pad.status == 7, "wrong bad-channel status decoding");
  require(pad.pedestalCounts == 1.0, "wrong packed pedestal decoding");
  const auto expectedLowSlope = 0.00215 * 740.3 / 652.8 * 0.85;
  require(close(pad.lowRangeSignalPerCount, expectedLowSlope),
          "wrong packed low-range slope decoding");
  require(close(pad.gainRatio, 5.0), "wrong packed gain ratio decoding");
  require(close(pad.highRangeSignalPerCount, 5.0 * expectedLowSlope),
          "wrong high-range slope");
  require(close(conditions.pad(1, 1, 2).gainRatio, 4.94),
          "legacy 0.494 gain-ratio correction was not preserved");
}
