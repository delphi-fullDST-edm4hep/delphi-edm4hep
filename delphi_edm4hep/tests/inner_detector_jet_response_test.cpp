#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Simulation/InnerDetectorJetResponse.h"
#include "delphi_edm4hep/Simulation/InnerDetectorReadoutGeometry.h"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool close(double left, double right, double tolerance = 1e-10) {
  return std::abs(left - right) <= tolerance;
}

} // namespace

int main() {
  std::ostringstream cargo;
  cargo << R"(*GEOM /ID**/JET*/0001.B
910401,120000,890905,4728
*SHAP 7,CYL1,-7.5,7.5,11.6,23,-59,59
**
*CALB /ID**.B
910401,0,910422,105300
*GASD 10,0,977.33,0,0,997.3,94.681,4.577,.619,69.941,30.058
**
*CALB /ID**/JET*.B
910401,0,910422,105302
*SLOW 22,5,-6.2,1.01,1,.1E-01,19.3,27.4,29,26.393,23.7537,100,26.1975,25.611,24.829,100,.96,.4E-01,0,2750,10,2770,2750
**
)";
  for (unsigned int sector = 1; sector <= 24; ++sector) {
    cargo << "*CALB /ID**/JET*/";
    cargo.width(4);
    cargo.fill('0');
    cargo << sector << ".B\n910401,0,910422,105303\n"
          << "*SLOW 8,5500,10,30," << (sector == 8 ? 5000 : 5500)
          << ",2300,10,30,2300\n";
    if (sector == 1) {
      cargo << "*SHAR 3,259,600,752\n";
    }
    cargo << "**\n";
  }
  // The public CARGO constructor is exercised by the production audit. Build
  // a complete miniature readout payload here so this test also locks the
  // calibration-word indexing used by SIFTOT.
  std::ostringstream sensors;
  sensors << cargo.str();
  for (unsigned int sector = 1; sector <= 24; ++sector) {
    sensors << "*CALB /ID**/JET*/";
    sensors.width(4);
    sensors.fill('0');
    sensors << sector << ".SENS$WIRE.B\n910401,0,910422,105304\n"
            << "*LEAD 12,24,2,0,1,3,23,0,0,0,0,0,0\n*LOCC 48,";
    for (unsigned int wire = 0; wire < 24; ++wire) {
      if (wire)
        sensors << ',';
      sensors << 12.5 + .4 * wire << ",0";
    }
    sensors << "\n*SIZC 1,80\n*CALW 504,";
    for (unsigned int wire = 0; wire < 24; ++wire) {
      if (wire)
        sensors << ',';
      sensors << 12.5 + .4 * wire
              << ",1.86,51.9,563,-184.979,15986.4,-851.86,.0159986,34.76"
              << ",1.86,-51.9,563,-184.979,-15986.4,-851.86,-.0159986,34.76"
              << ",0,.25,.5,.75";
    }
    sensors
        << "\n*STAT 24,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n**\n";
  }
  for (unsigned int layer = 1; layer <= 5; ++layer) {
    for (const char *sensor : {"WIRE", "STRP"}) {
      sensors << "*CALB /ID**/TRIG/";
      sensors.width(4);
      sensors.fill('0');
      sensors << layer << ".SENS$" << sensor << ".B\n910401,0,910422,105400\n"
              << "*LEAD 12,192,0,0,0,0,0,0,0,0,0,0,0\n"
              << "*LOCC 3," << (sensor[0] == 'W' ? 23.0 + layer : 23.4 + layer)
              << ',' << (sensor[0] == 'W' ? 0.0 : -39.8) << ','
              << (sensor[0] == 'W' ? 1.875 : .4166666667) << "\n";
      if (sensor[0] == 'W')
        sensors << "*SIZC 1,80\n";
      sensors << "*CALW 384";
      for (unsigned int channel = 0; channel < 384; ++channel)
        sensors << ",0";
      sensors << "\n*STAT 192";
      for (unsigned int channel = 0; channel < 192; ++channel)
        sensors << ",0";
      sensors << "\n**\n";
    }
  }
  std::istringstream completeInput(sensors.str());
  const auto completeDatabase =
      delphi_edm4hep::geometry::CargoDatabase::read(completeInput, "fixture");
  const auto readout =
      delphi_edm4hep::simulation::InnerDetectorReadoutGeometry::fromCargo(
          completeDatabase);
  const auto response =
      delphi_edm4hep::simulation::InnerDetectorJetResponse::fromCargo(
          completeDatabase, readout, 1.2312434);

  for (std::uint32_t sector = 1; sector <= 24; ++sector) {
    const auto phi = readout.jetSectorMidPhi(sector);
    const auto address =
        readout.locateJet(16.9 * std::cos(phi), 16.9 * std::sin(phi), 0.0);
    require(address && address->sector == sector && address->wire == 12 &&
                address->side ==
                    delphi_edm4hep::simulation::InnerDetectorDriftSide::Left,
            "ID jet centre locator changed");
    const auto cellID = delphi_edm4hep::simulation::
        InnerDetectorReadoutGeometry::encodeJetCellID(*address);
    const auto decoded = delphi_edm4hep::simulation::
        InnerDetectorReadoutGeometry::decodeJetCellID(cellID);
    require(decoded.sector == address->sector &&
                decoded.wire == address->wire && decoded.side == address->side,
            "ID jet cell-ID round trip changed");
  }
  const auto outwardCrossings =
      readout.jetWireCrossings(std::array<double, 3>{12.0, 0.0, 0.0},
                               std::array<double, 3>{22.0, 0.0, 0.0});
  require(outwardCrossings.size() == 24,
          "radial segment did not cross every ID jet wire");
  for (std::size_t crossing = 0; crossing < outwardCrossings.size();
       ++crossing) {
    require(outwardCrossings[crossing].address.sector == 1 &&
                outwardCrossings[crossing].address.wire == crossing + 1 &&
                outwardCrossings[crossing].pathFraction >= 0.0 &&
                outwardCrossings[crossing].pathFraction <= 1.0,
            "ID jet wire crossing order changed");
  }

  require(close(response.boundaryAngleRadians(), 5.0 * std::acos(-1.0) / 180.0),
          "wrong boundary angle");
  require(response.lorentzAngleRadians() < -6.2 * std::acos(-1.0) / 180.0,
          "magnetic-field Lorentz correction was not applied");
  require(
      close(response.driftTimeNs(
                1, 1, delphi_edm4hep::simulation::InnerDetectorDriftSide::Right,
                0.0),
            1.86),
      "right near-wire calibration branch changed");
  require(
      close(response.driftTimeNs(
                1, 1, delphi_edm4hep::simulation::InnerDetectorDriftSide::Left,
                0.0),
            1.86),
      "left near-wire calibration branch changed");
  require(response.velocityCorrections(7)[3] >
              response.velocityCorrections(8)[3],
          "sector fence-voltage correction was not applied");
  require(close(response.velocityCorrections(7)[0],
                response.velocityCorrections(8)[3]),
          "left slow region did not use the next sector fence");
  require(response.maximumDriftTimeNs() > 100.0,
          "ID maximum drift time is not physical");
  const auto nearWire = response.coordinateFromDriftTime(
      1, 1, delphi_edm4hep::simulation::InnerDetectorDriftSide::Left,
      response.driftTimeNs(
          1, 1, delphi_edm4hep::simulation::InnerDetectorDriftSide::Left, 0.0) -
          1.0);
  require(nearWire && close(nearWire->radiusCm, 12.5) &&
              close(nearWire->localPhiRadians, 0.0),
          "near-wire TDC underflow was not clamped to the wire");

  unsigned int gapClamps{};
  for (const auto side :
       {delphi_edm4hep::simulation::InnerDetectorDriftSide::Left,
        delphi_edm4hep::simulation::InnerDetectorDriftSide::Right}) {
    const auto sign =
        side == delphi_edm4hep::simulation::InnerDetectorDriftSide::Left ? -1.0
                                                                         : 1.0;
    for (unsigned int sample = 0; sample <= 20; ++sample) {
      const auto phi = sign * sample * std::acos(-1.0) / (20.0 * 24.0);
      const auto time = response.driftTimeNs(1, 12, side, phi);
      const auto count = response.tdcCount(1, 12, time);
      const auto quantized = response.driftTimeFromTdcCount(1, 12, count);
      require(std::abs(quantized - time) < 3.0,
              "SITTOC/SICTOT quantization changed");
      const auto coordinate =
          response.coordinateFromDriftTime(1, 12, side, quantized);
      require(coordinate.has_value(), "drift-time inversion failed");
      gapClamps += !close(coordinate->localPhiRadians, phi, 5e-4);
    }
  }
  require(gapClamps == 2,
          "SITTOF drift-gap clamp count changed: " + std::to_string(gapClamps));
}
