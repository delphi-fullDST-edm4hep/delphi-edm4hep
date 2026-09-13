#include "delphi_edm4hep/Simulation/VertexStripReadout.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace delphi_edm4hep::simulation {
namespace {

int orientation(const VertexSensor &sensor) {
  switch (sensor.layer) {
  case VertexBarrelLayer::Closer:
    return sensor.longitudinalRegion == VertexLongitudinalRegion::Central ? -1
                                                                          : 1;
  case VertexBarrelLayer::Inner:
    return -1;
  case VertexBarrelLayer::Outer:
    return sensor.longitudinalRegion == VertexLongitudinalRegion::Central ? 1
                                                                          : -1;
  }
  throw std::runtime_error("invalid VD layer in strip orientation");
}

double pOrigin(const VertexSensor &sensor) {
  const auto &readout = sensor.readout;
  return (static_cast<double>(readout.pReadoutChannels) - 1.0) *
             readout.pPhysicalPitchCm +
         orientation(sensor) * sensor.halfModuleSign * 0.5 *
             readout.pPhysicalPitchCm;
}

double nLength(const VertexSensor &sensor) {
  const auto &readout = sensor.readout;
  if (!readout.nReadoutEnabled()) {
    return 0.0;
  }
  return readout.nFirstPitchChannels * readout.nFirstPitchCm +
         (readout.nReadoutChannels - readout.nFirstPitchChannels) *
             readout.nSecondPitchCm +
         readout.nSecondZoneOffsetCm;
}

double nOrigin(const VertexSensor &sensor) {
  switch (sensor.layer) {
  case VertexBarrelLayer::Closer:
    if (sensor.longitudinalRegion == VertexLongitudinalRegion::Central) {
      return (2.0 * 384.0 - 0.5) * 0.00495;
    }
    return (0.5 * 384.0 + 0.5) * 0.0150;
  case VertexBarrelLayer::Inner:
    return 0.5 * sensor.pActiveLine.lengthCm();
  case VertexBarrelLayer::Outer:
    return 0.5 * sensor.readout.nReadoutChannels *
               sensor.readout.nFirstPitchCm +
           0.5 * sensor.readout.nFirstPitchCm;
  }
  throw std::runtime_error("invalid VD layer in N origin");
}

std::array<double, 3> midpoint(const VertexActiveLine &line) {
  return {(line.firstCm[0] + line.secondCm[0]) / 2.0,
          (line.firstCm[1] + line.secondCm[1]) / 2.0,
          (line.firstCm[2] + line.secondCm[2]) / 2.0};
}

std::uint32_t nStrip(const VertexSensor &sensor, double coordinateCm) {
  const auto &readout = sensor.readout;
  if (readout.nSecondPitchCm > 0 &&
      coordinateCm >= readout.nFirstPitchChannels * readout.nFirstPitchCm) {
    const auto second = std::floor(
        (coordinateCm - (readout.nFirstPitchChannels * readout.nFirstPitchCm +
                         readout.nSecondZoneOffsetCm)) /
        readout.nSecondPitchCm);
    return readout.nFirstPitchChannels +
           static_cast<std::uint32_t>(std::max(0.0, second)) + 1;
  }
  return static_cast<std::uint32_t>(
             std::max(0.0, std::floor(coordinateCm / readout.nFirstPitchCm))) +
         1;
}

double nStripCenter(const VertexSensor &sensor, std::uint32_t strip) {
  const auto &readout = sensor.readout;
  if (readout.nSecondPitchCm > 0 && strip > readout.nFirstPitchChannels) {
    return readout.nFirstPitchChannels * readout.nFirstPitchCm +
           (static_cast<double>(strip - readout.nFirstPitchChannels) - 0.5) *
               readout.nSecondPitchCm +
           readout.nSecondZoneOffsetCm;
  }
  return (static_cast<double>(strip) - 0.5) * readout.nFirstPitchCm;
}

} // namespace

std::optional<VertexStripAddress>
VertexStripReadout::locate(const VertexSensor &sensor, VertexReadoutSide side,
                           const std::array<double, 3> &globalCm) const {
  const auto local = sensor.pTransform.globalToLocal(globalCm);
  if (side == VertexReadoutSide::P) {
    auto coordinate = local[0] + pOrigin(sensor);
    coordinate -= orientation(sensor) * conditions_.lorentzShiftCm();
    const auto width =
        sensor.readout.pReadoutChannels * sensor.readout.pReadoutPitchCm;
    if (coordinate < 0 || coordinate > width) {
      return std::nullopt;
    }
    const auto stagger = (sensor.halfModuleSign * orientation(sensor) + 1) / 2;
    const auto geometric = std::llround(
        (coordinate / sensor.readout.pPhysicalPitchCm - stagger + 1.5) / 2.0);
    if (geometric < 1 || geometric > static_cast<std::int64_t>(
                                         sensor.readout.pReadoutChannels)) {
      return std::nullopt;
    }
    return VertexStripAddress{sensor.semanticSensor, side,
                              sensor.readout.pReadoutChannels -
                                  static_cast<std::uint32_t>(geometric) + 1};
  }

  if (!sensor.readout.nReadoutEnabled()) {
    return std::nullopt;
  }
  const auto coordinate = sensor.halfModuleSign * local[2] + nOrigin(sensor);
  if (coordinate < 0 || coordinate > nLength(sensor)) {
    return std::nullopt;
  }
  const auto strip = nStrip(sensor, coordinate);
  if (strip < 1 || strip > sensor.readout.nReadoutChannels) {
    return std::nullopt;
  }
  return VertexStripAddress{sensor.semanticSensor, side, strip};
}

std::array<double, 3>
VertexStripReadout::measurementCenter(const VertexSensor &sensor,
                                      VertexReadoutSide side,
                                      std::uint32_t strip) const {
  const auto maximum = side == VertexReadoutSide::P
                           ? sensor.readout.pReadoutChannels
                           : sensor.readout.nReadoutChannels;
  if (strip < 1 || strip > maximum) {
    throw std::out_of_range("VD strip is outside the sensor readout");
  }
  if (side == VertexReadoutSide::P) {
    auto local = sensor.pTransform.globalToLocal(midpoint(sensor.pActiveLine));
    const auto geometric = sensor.readout.pReadoutChannels - strip + 1;
    const auto stagger = (sensor.halfModuleSign * orientation(sensor) + 1) / 2;
    const auto coordinate =
        (2.0 * geometric + stagger - 1.5) * sensor.readout.pPhysicalPitchCm;
    local[0] = coordinate - pOrigin(sensor) +
               orientation(sensor) * conditions_.lorentzShiftCm();
    local[1] = 0.0;
    return sensor.pTransform.localToGlobal(local);
  }

  if (!sensor.nActiveLine) {
    throw std::invalid_argument("VD sensor has no N-side active line");
  }
  auto local = sensor.pTransform.globalToLocal(midpoint(*sensor.nActiveLine));
  local[1] = 0.0;
  local[2] =
      sensor.halfModuleSign * (nStripCenter(sensor, strip) - nOrigin(sensor));
  return sensor.pTransform.localToGlobal(local);
}

std::uint64_t
VertexStripReadout::encodeCellID(const VertexStripAddress &address) {
  if (address.semanticSensor < 1 || address.semanticSensor > 0x00ffffffU ||
      address.strip < 1 || address.strip > 0x7ffU) {
    throw std::out_of_range("VD strip address exceeds cell-ID fields");
  }
  return VertexReadoutGeometry::cellIDBase(address.semanticSensor) |
         (static_cast<std::uint64_t>(address.side == VertexReadoutSide::N)
          << 31U) |
         address.strip;
}

VertexStripAddress VertexStripReadout::decodeCellID(std::uint64_t cellID) {
  if ((cellID & 0x7ffff800ULL) != 0) {
    throw std::invalid_argument("VD strip cell ID has unknown low bits");
  }
  const auto strip = static_cast<std::uint32_t>(cellID & 0x7ffU);
  if (strip == 0) {
    throw std::invalid_argument("VD strip cell ID has a zero strip");
  }
  return {VertexReadoutGeometry::semanticSensor(cellID),
          (cellID >> 31U) & 1U ? VertexReadoutSide::N : VertexReadoutSide::P,
          strip};
}

} // namespace delphi_edm4hep::simulation
