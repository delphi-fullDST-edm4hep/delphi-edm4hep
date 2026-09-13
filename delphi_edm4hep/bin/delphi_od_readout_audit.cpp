#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Simulation/OuterDetectorDriftResponse.h"
#include "delphi_edm4hep/Simulation/OuterDetectorReadoutGeometry.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " CERNSNAP*_DELSIM.ASC\n";
    return 2;
  }
  try {
    using namespace delphi_edm4hep;
    const auto database = geometry::CargoDatabase::readFile(argv[1]);
    const auto readout =
        simulation::OuterDetectorReadoutGeometry::fromCargo(database);
    simulation::OuterDetectorDriftResponse response;
    unsigned int active{};
    unsigned int roundTripMismatches{};
    unsigned int locatorMismatches{};
    double minimumEfficiency = 1.0;
    double maximumEfficiency{};
    for (const auto &tube : readout.tubes()) {
      active += tube.active;
      minimumEfficiency = std::min(minimumEfficiency, tube.efficiency);
      maximumEfficiency = std::max(maximumEfficiency, tube.efficiency);
      const auto decoded =
          simulation::OuterDetectorReadoutGeometry::decodeChannelID(
              simulation::OuterDetectorReadoutGeometry::encodeChannelID(
                  tube.channel));
      roundTripMismatches += decoded.plank != tube.channel.plank ||
                             decoded.layer != tube.channel.layer ||
                             decoded.column != tube.channel.column;
      const auto position = readout.wirePosition(tube, 0.0);
      const auto located = readout.locate(position[0], position[1], 0.0);
      locatorMismatches += !located ||
                           located->tube->channel.plank != tube.channel.plank ||
                           located->tube->channel.layer != tube.channel.layer ||
                           located->tube->channel.column != tube.channel.column;
    }
    const auto &first = readout.tubes().front();
    const auto &last = readout.tubes().back();
    std::cout << "tubes=" << readout.tubes().size() << '\n'
              << "planks=" << readout.tubes().size() / 145 << '\n'
              << "layers=5\n"
              << "wires_per_plank=145\n"
              << "cell_pitch_x_cm=" << readout.cellPitchXCm() << '\n'
              << "layer_pitch_y_cm=" << readout.layerPitchYCm() << '\n'
              << "gas_width_cm=" << readout.gasWidthCm() << '\n'
              << "gas_height_cm=" << readout.gasHeightCm() << '\n'
              << "active_channels=" << active << '\n'
              << "minimum_efficiency=" << minimumEfficiency << '\n'
              << "maximum_efficiency=" << maximumEfficiency << '\n'
              << "first_channel=" << first.channel.plank << ':'
              << first.channel.layer << ':' << first.channel.column << '\n'
              << "last_channel=" << last.channel.plank << ':'
              << last.channel.layer << ':' << last.channel.column << '\n'
              << "first_pedestal_ns=" << first.pedestalNs << '\n'
              << "first_z_propagation_ns=" << first.zPropagationNs << '\n'
              << "first_pulse_width_ns=" << first.pulseWidthNs << '\n'
              << "drift_0p5cm_0deg_ns=" << response.driftTimeNs(0.5, 0.0)
              << '\n'
              << "drift_0p5cm_45deg_ns="
              << response.driftTimeNs(0.5, std::acos(-1.0) / 4.0) << '\n'
              << "address_round_trip_mismatches=" << roundTripMismatches
              << '\n'
              << "wire_locator_mismatches=" << locatorMismatches << '\n';
  } catch (const std::exception &error) {
    std::cerr << "delphi_od_readout_audit: " << error.what() << '\n';
    return 1;
  }
}
