#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GeometryModel.h"
#include "delphi_edm4hep/Simulation/VertexReadoutGeometry.h"
#include "delphi_edm4hep/Simulation/VertexStripReadout.h"

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
    const auto model = geometry::GeometryModel::fromCargo(database, argv[1]);
    const auto readout =
        simulation::VertexReadoutGeometry::fromCargo(database, model);
    const simulation::VertexStripReadout stripReadout;
    std::array<unsigned int, 3> layerSensors{};
    unsigned int nSideSensors{};
    unsigned int centralSensors{};
    unsigned int innerOdd512{};
    unsigned int roundTripMismatches{};
    std::uint64_t readoutAddresses{};
    std::uint64_t stripRoundTripMismatches{};
    std::array<std::array<std::uint64_t, 2>, 3> stripMismatchesByLayer{};
    for (const auto &sensor : readout.sensors()) {
      ++layerSensors[static_cast<std::size_t>(sensor.layer) - 1];
      nSideSensors += sensor.nTransform.has_value();
      centralSensors += sensor.longitudinalRegion ==
                        simulation::VertexLongitudinalRegion::Central;
      innerOdd512 += sensor.layer == simulation::VertexBarrelLayer::Inner &&
                     sensor.module % 2 == 1 &&
                     sensor.readout.pReadoutChannels == 512;
      const std::array<double, 3> local{0.31, -0.002, 0.47};
      const auto recovered = sensor.pTransform.globalToLocal(
          sensor.pTransform.localToGlobal(local));
      for (std::size_t coordinate = 0; coordinate < 3; ++coordinate) {
        if (std::abs(recovered[coordinate] - local[coordinate]) > 2.0e-5) {
          ++roundTripMismatches;
          break;
        }
      }
      if (sensor.cellIDBase != simulation::VertexReadoutGeometry::cellIDBase(
                                   sensor.semanticSensor) ||
          &readout.sensorForTransportCellID(sensor.cellIDBase | 1234U) !=
              &sensor) {
        ++roundTripMismatches;
      }
      for (std::uint32_t strip = 1; strip <= sensor.readout.pReadoutChannels;
           ++strip) {
        static_cast<void>(readout.electronicsAddress(
            sensor, simulation::VertexReadoutSide::P, strip));
        ++readoutAddresses;
        const simulation::VertexStripAddress expected{
            sensor.semanticSensor, simulation::VertexReadoutSide::P, strip};
        const auto decoded = simulation::VertexStripReadout::decodeCellID(
            simulation::VertexStripReadout::encodeCellID(expected));
        const auto located = stripReadout.locate(
            sensor, simulation::VertexReadoutSide::P,
            stripReadout.measurementCenter(
                sensor, simulation::VertexReadoutSide::P, strip));
        if (!located || decoded.semanticSensor != expected.semanticSensor ||
            decoded.side != expected.side || decoded.strip != expected.strip ||
            located->strip != strip) {
          ++stripRoundTripMismatches;
          ++stripMismatchesByLayer[static_cast<std::size_t>(sensor.layer) - 1]
                                  [0];
        }
      }
      for (std::uint32_t strip = 1; strip <= sensor.readout.nReadoutChannels;
           ++strip) {
        static_cast<void>(readout.electronicsAddress(
            sensor, simulation::VertexReadoutSide::N, strip));
        ++readoutAddresses;
        const simulation::VertexStripAddress expected{
            sensor.semanticSensor, simulation::VertexReadoutSide::N, strip};
        const auto decoded = simulation::VertexStripReadout::decodeCellID(
            simulation::VertexStripReadout::encodeCellID(expected));
        const auto located = stripReadout.locate(
            sensor, simulation::VertexReadoutSide::N,
            stripReadout.measurementCenter(
                sensor, simulation::VertexReadoutSide::N, strip));
        if (!located || decoded.semanticSensor != expected.semanticSensor ||
            decoded.side != expected.side || decoded.strip != expected.strip ||
            located->strip != strip) {
          ++stripRoundTripMismatches;
          ++stripMismatchesByLayer[static_cast<std::size_t>(sensor.layer) - 1]
                                  [1];
        }
      }
    }
    const auto &sensor22 = readout.sensor(22);
    const auto sensor22PFirst = readout.electronicsAddress(
        sensor22, simulation::VertexReadoutSide::P, 1);
    const auto sensor22PLast =
        readout.electronicsAddress(sensor22, simulation::VertexReadoutSide::P,
                                   sensor22.readout.pReadoutChannels);
    const auto sensor22NFirst = readout.electronicsAddress(
        sensor22, simulation::VertexReadoutSide::N, 1);
    const auto sensor22NLast =
        readout.electronicsAddress(sensor22, simulation::VertexReadoutSide::N,
                                   sensor22.readout.nReadoutChannels);
    std::cout
        << "sensors=" << readout.sensors().size() << '\n'
        << "closer_sensors=" << layerSensors[0] << '\n'
        << "inner_sensors=" << layerSensors[1] << '\n'
        << "outer_sensors=" << layerSensors[2] << '\n'
        << "n_side_sensors=" << nSideSensors << '\n'
        << "central_sensors=" << centralSensors << '\n'
        << "inner_odd_512_channel_sensors=" << innerOdd512 << '\n'
        << "readout_addresses=" << readoutAddresses << '\n'
        << "strip_round_trip_mismatches=" << stripRoundTripMismatches << '\n'
        << "strip_mismatch_breakdown=" << stripMismatchesByLayer[0][0] << ','
        << stripMismatchesByLayer[0][1] << ',' << stripMismatchesByLayer[1][0]
        << ',' << stripMismatchesByLayer[1][1] << ','
        << stripMismatchesByLayer[2][0] << ',' << stripMismatchesByLayer[2][1]
        << '\n'
        << "sensor22_path=" << sensor22.path << '\n'
        << "sensor22_x_cm=" << sensor22.pTransform.translationCm[0] << '\n'
        << "sensor22_y_cm=" << sensor22.pTransform.translationCm[1] << '\n'
        << "sensor22_z_cm=" << sensor22.pTransform.translationCm[2] << '\n'
        << "sensor22_p_active_length_cm=" << sensor22.pActiveLine.lengthCm()
        << '\n'
        << "sensor22_p_electronics=" << sensor22PFirst.sirocco << ':'
        << sensor22PFirst.channel << '-' << sensor22PLast.channel << '\n'
        << "sensor22_n_electronics=" << sensor22NFirst.sirocco << ':'
        << sensor22NFirst.channel << '-' << sensor22NLast.channel << '\n'
        << "transform_round_trip_mismatches=" << roundTripMismatches << '\n';
  } catch (const std::exception &error) {
    std::cerr << "delphi_vertex_readout_audit: " << error.what() << '\n';
    return 1;
  }
}
