#include "delphi_edm4hep/Simulation/InnerDetectorJetResponse.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace delphi_edm4hep::simulation {
namespace {

constexpr double degrees = std::numbers::pi / 180.0;
constexpr double calibrationTemperatureKelvin = 273.15 + 20.0;
constexpr double calibrationPressureBar = 1.0;
constexpr double calibrationMagneticFieldTesla = 1.2;
constexpr double calibrationFastVoltage = 2330.0;

const geometry::CargoRecord &record(const geometry::CargoDatabase &database,
                                    const std::string &path) {
  const geometry::CargoRecord *found{};
  for (const auto &candidate : database.records()) {
    if (candidate.kind == "CALB" && candidate.path == path) {
      if (found != nullptr) {
        throw std::runtime_error("duplicate ID calibration record: " + path);
      }
      found = &candidate;
    }
  }
  if (found == nullptr) {
    throw std::runtime_error("missing ID calibration record: " + path);
  }
  return *found;
}

std::vector<double> values(const geometry::CargoRecord &source,
                           std::string_view fieldName) {
  const auto *field = source.findField(fieldName);
  if (field == nullptr) {
    throw std::runtime_error("missing ID " + std::string(fieldName) +
                             " field: " + source.path);
  }
  auto text = field->value;
  for (const auto &line : field->continuation) {
    text += ' ';
    text += line;
  }
  std::replace(text.begin(), text.end(), ',', ' ');
  std::replace(text.begin(), text.end(), 'D', 'E');
  std::replace(text.begin(), text.end(), 'd', 'e');
  std::istringstream input(text);
  std::size_t count{};
  if (!(input >> count)) {
    throw std::runtime_error("invalid ID field count: " + source.path);
  }
  std::vector<double> result;
  double value{};
  while (input >> value) {
    result.push_back(value);
  }
  if (result.size() != count) {
    throw std::runtime_error("ID field count mismatch: " + source.path);
  }
  return result;
}

std::string sectorPath(unsigned int sector) {
  std::ostringstream path;
  path << "/ID**/JET*/";
  path.width(4);
  path.fill('0');
  path << sector << ".B";
  return path.str();
}

double canonicalFenceVoltage(double voltage) {
  if (std::abs(voltage - 5500.0) < 250.0) {
    return 5500.0;
  }
  if (std::abs(voltage - 5000.0) < 250.0) {
    return 5000.0;
  }
  if (std::abs(voltage - 4500.0) < 250.0) {
    return 4500.0;
  }
  return voltage;
}

double calibratedRadius(double radius, double phi, double angle) {
  return radius * (1.0 / std::cos(phi) +
                   std::tan(phi) * std::sin(angle) / std::cos(angle + phi));
}

double polynomial(double c1, double c2, double c3, double value) {
  return c1 + (c2 + c3 * value) * value;
}

std::optional<double> inversePolynomial(double c1, double c2, double c3,
                                        double value) {
  if (c3 == 0.0) {
    if (c2 == 0.0) {
      return std::nullopt;
    }
    return (value - c1) / c2;
  }
  const auto discriminant = c2 * c2 - 4.0 * c3 * (c1 - value);
  if (discriminant < 0.0) {
    return std::nullopt;
  }
  double sign{};
  if (c2 > 0.0) {
    sign = 1.0;
  } else if (c2 < 0.0) {
    sign = -1.0;
  } else if (c1 > 0.0) {
    sign = 1.0;
  } else {
    return std::nullopt;
  }
  return (-c2 + sign * std::sqrt(discriminant)) / (2.0 * c3);
}

} // namespace

InnerDetectorJetResponse
InnerDetectorJetResponse::fromCargo(const geometry::CargoDatabase &database,
                                    const InnerDetectorReadoutGeometry &readout,
                                    double magneticFieldTesla) {
  if (!std::isfinite(magneticFieldTesla)) {
    throw std::invalid_argument("ID magnetic field must be finite");
  }

  InnerDetectorJetResponse result;
  result.readout_ = readout;
  result.magneticFieldTesla_ = magneticFieldTesla;
  result.lorentzAngleRadians_ =
      std::atan((magneticFieldTesla / calibrationMagneticFieldTesla) *
                std::tan(-6.2 * degrees));

  const auto jetSlow = values(record(database, "/ID**/JET*.B"), "SLOW");
  if (jetSlow.size() != 22) {
    throw std::runtime_error("invalid ID global slow-control payload");
  }
  result.boundaryAngleRadians_ = jetSlow[0] * degrees;
  if (std::abs(jetSlow[0] - 5.0) > 0.01) {
    result.boundaryAngleRadians_ = 5.0 * degrees;
  }

  auto temperatureKelvin = jetSlow[5] + 273.15;
  if (temperatureKelvin < 288.0 || temperatureKelvin > 298.0) {
    temperatureKelvin = calibrationTemperatureKelvin;
  }
  const auto gas = values(record(database, "/ID**.B"), "GASD");
  if (gas.size() != 10) {
    throw std::runtime_error("invalid ID gas payload");
  }
  auto pressureBar = gas[4] / 1000.0;
  if (pressureBar < 0.9 || pressureBar > 1.1) {
    pressureBar = 0.9965;
  }

  std::array<double, 24> fenceVoltages{};
  std::array<unsigned int, 3> voltageCounts{}; // 4500, 5000, 5500
  for (unsigned int sector = 1; sector <= fenceVoltages.size(); ++sector) {
    const auto slow = values(record(database, sectorPath(sector)), "SLOW");
    if (slow.size() != 7 && slow.size() != 8) {
      throw std::runtime_error("invalid ID sector slow-control payload: " +
                               sectorPath(sector));
    }
    const auto rawVoltage = slow.size() == 8 ? slow[3] : 5500.0;
    const auto voltage = canonicalFenceVoltage(rawVoltage);
    fenceVoltages[sector - 1] = voltage;
    if (voltage == 4500.0) {
      ++voltageCounts[0];
    } else if (voltage == 5000.0) {
      ++voltageCounts[1];
    } else if (voltage == 5500.0) {
      ++voltageCounts[2];
    }
  }

  double modalFenceVoltage{};
  if (voltageCounts[0] >= voltageCounts[1] &&
      voltageCounts[0] >= voltageCounts[2]) {
    modalFenceVoltage = 4500.0;
  }
  if (voltageCounts[1] >= voltageCounts[0] &&
      voltageCounts[1] >= voltageCounts[2]) {
    modalFenceVoltage = 5000.0;
  }
  if (voltageCounts[2] >= voltageCounts[0] &&
      voltageCounts[2] >= voltageCounts[1]) {
    modalFenceVoltage = 5500.0;
  }
  for (std::size_t sector = 0; sector < fenceVoltages.size(); ++sector) {
    if (sector == 16 || fenceVoltages[sector] < 4250.0) {
      fenceVoltages[sector] = modalFenceVoltage;
    }
  }

  const auto densityCorrection =
      (temperatureKelvin / calibrationTemperatureKelvin) /
      (pressureBar / calibrationPressureBar);
  const auto fastCorrection = 2275.0 / calibrationFastVoltage;
  for (std::size_t sector = 0; sector < result.velocityCorrections_.size();
       ++sector) {
    const auto nextSector = (sector + 1) % fenceVoltages.size();
    auto &correction = result.velocityCorrections_[sector];
    correction[0] = fenceVoltages[nextSector] / 5500.0; // left slow
    correction[1] = fastCorrection;                     // left fast
    correction[2] = fastCorrection;                     // right fast
    correction[3] = fenceVoltages[sector] / 5500.0;     // right slow
    for (auto &value : correction) {
      value *= densityCorrection;
    }
  }
  return result;
}

const std::array<double, 4> &
InnerDetectorJetResponse::velocityCorrections(std::uint32_t sector) const {
  if (sector < 1 || sector > velocityCorrections_.size()) {
    throw std::out_of_range("invalid ID jet sector");
  }
  return velocityCorrections_[sector - 1];
}

double InnerDetectorJetResponse::driftTimeNs(std::uint32_t sector,
                                             std::uint32_t wire,
                                             InnerDetectorDriftSide side,
                                             double localPhiRadians) const {
  if (sector < 1 || sector > readout_.jetSectors().size() || wire < 1 ||
      wire > readout_.jetSectors()[sector - 1].wires.size() ||
      !std::isfinite(localPhiRadians)) {
    throw std::out_of_range("invalid ID jet drift coordinate");
  }

  const auto &calibration =
      readout_.jetSectors()[sector - 1].wires[wire - 1].calibration;
  const auto &correction = velocityCorrections(sector);
  const bool right = side == InnerDetectorDriftSide::Right;
  const std::size_t offset = right ? 0 : 8;
  const auto beta =
      right ? boundaryAngleRadians_ : std::numbers::pi - boundaryAngleRadians_;
  const auto band = right ? boundaryHalfWidthCm_ : -boundaryHalfWidthCm_;
  const auto fast = right ? correction[2] : correction[1];
  const auto slow = right ? correction[3] : correction[0];
  const auto wireRadius = calibration[0];

  const auto transitionPhi = std::abs(
      std::atan2(band, wireRadius + band * std::tan(lorentzAngleRadians_)));
  double x0{};
  double y0{};
  if (std::abs(localPhiRadians) <= transitionPhi) {
    const auto radius =
        calibratedRadius(wireRadius, localPhiRadians, lorentzAngleRadians_);
    x0 = radius * std::sin(localPhiRadians);
    y0 = radius * std::cos(localPhiRadians) -
         band * std::tan(lorentzAngleRadians_);
  } else {
    const auto radius =
        calibratedRadius(wireRadius + band * std::tan(lorentzAngleRadians_) -
                             band / std::tan(localPhiRadians),
                         localPhiRadians, lorentzAngleRadians_ + beta) +
        band / std::sin(localPhiRadians);
    const auto x = radius * std::sin(localPhiRadians);
    const auto y = radius * std::cos(localPhiRadians);
    const auto distance =
        std::hypot(x - band,
                   y - wireRadius - band * std::tan(lorentzAngleRadians_)) *
        std::sin(lorentzAngleRadians_);
    x0 = x + distance * std::sin(beta);
    y0 = y - band * std::tan(lorentzAngleRadians_) - distance * std::cos(beta);
  }

  const auto correctedPhi = std::atan2(x0, y0);
  const auto transitionCalibration = calibration[7 + offset];
  if (std::abs(correctedPhi) <= std::abs(transitionCalibration)) {
    return polynomial(calibration[1 + offset], calibration[2 + offset] / fast,
                      calibration[3 + offset] / (fast * fast), x0);
  }
  return polynomial(calibration[4 + offset], calibration[5 + offset] / slow,
                    calibration[6 + offset] / (slow * slow), correctedPhi);
}

std::optional<InnerDetectorJetCoordinate>
InnerDetectorJetResponse::coordinateFromDriftTime(std::uint32_t sector,
                                                  std::uint32_t wire,
                                                  InnerDetectorDriftSide side,
                                                  double driftTimeNs) const {
  if (sector < 1 || sector > readout_.jetSectors().size() || wire < 1 ||
      wire > readout_.jetSectors()[sector - 1].wires.size() ||
      !std::isfinite(driftTimeNs)) {
    throw std::out_of_range("invalid ID jet drift coordinate");
  }

  const auto &calibration =
      readout_.jetSectors()[sector - 1].wires[wire - 1].calibration;
  const auto &correction = velocityCorrections(sector);
  const bool right = side == InnerDetectorDriftSide::Right;
  const std::size_t offset = right ? 0 : 8;
  const auto beta =
      right ? boundaryAngleRadians_ : std::numbers::pi - boundaryAngleRadians_;
  const auto band = right ? boundaryHalfWidthCm_ : -boundaryHalfWidthCm_;
  const auto fast = right ? correction[2] : correction[1];
  const auto slow = right ? correction[3] : correction[0];
  const auto wireRadius = calibration[0];

  // The four-bin fine TDC can quantize a hit just below the calibrated
  // near-wire intercept. That is a valid wire hit, not a failed polynomial
  // inversion: place it at zero drift distance. This is also the natural
  // saturation behaviour for an earlier raw count.
  if (driftTimeNs <= calibration[1 + offset]) {
    return InnerDetectorJetCoordinate{wireRadius, 0.0};
  }

  double x{};
  double y{};
  if (driftTimeNs <= calibration[8 + offset]) {
    const auto x0 = inversePolynomial(
        calibration[1 + offset], calibration[2 + offset] / fast,
        calibration[3 + offset] / (fast * fast), driftTimeNs);
    if (!x0) {
      return std::nullopt;
    }
    x = *x0;
    y = wireRadius + *x0 * std::tan(lorentzAngleRadians_);
  } else {
    const auto correctedPhi = inversePolynomial(
        calibration[4 + offset], calibration[5 + offset] / slow,
        calibration[6 + offset] / (slow * slow), driftTimeNs);
    if (!correctedPhi || *correctedPhi == 0.0) {
      return std::nullopt;
    }
    const auto radius0 =
        calibratedRadius(wireRadius - band / std::tan(*correctedPhi),
                         *correctedPhi, beta) +
        band / std::sin(*correctedPhi);
    const auto x0 = radius0 * std::sin(*correctedPhi);
    const auto y0 = radius0 * std::cos(*correctedPhi);
    if (std::abs(x0) < boundaryHalfWidthCm_) {
      x = band;
      y = wireRadius + x * std::tan(lorentzAngleRadians_);
    } else {
      const auto distance = std::hypot(x0 - band, y0 - wireRadius) *
                            std::tan(lorentzAngleRadians_);
      x = x0 - distance * std::sin(beta);
      y = y0 + band * std::tan(lorentzAngleRadians_) +
          distance * std::cos(beta);
    }
  }
  return InnerDetectorJetCoordinate{std::hypot(x, y), std::atan2(x, y)};
}

std::int32_t InnerDetectorJetResponse::tdcCount(std::uint32_t sector,
                                                std::uint32_t wire,
                                                double driftTimeNs) const {
  if (sector < 1 || sector > readout_.jetSectors().size() || wire < 1 ||
      wire > readout_.jetSectors()[sector - 1].wires.size() ||
      !std::isfinite(driftTimeNs)) {
    throw std::out_of_range("invalid ID jet TDC coordinate");
  }
  constexpr double thirdRfFrequencyMHz = 351.0 / 3.0;
  const auto &calibration =
      readout_.jetSectors()[sector - 1].wires[wire - 1].calibration;
  const auto encodedTime = (driftTimeNs + readout_.driftTimeZeroNs()) *
                               thirdRfFrequencyMHz / 1000.0 +
                           0.125;
  const auto high = static_cast<std::int32_t>(encodedTime);
  const auto lowFraction = encodedTime - high;
  std::int32_t low{};
  if (lowFraction >= calibration[20]) {
    low = 3;
  } else if (lowFraction >= calibration[19]) {
    low = 2;
  } else if (lowFraction >= calibration[18]) {
    low = 1;
  }
  return 4 * high + low;
}

double InnerDetectorJetResponse::driftTimeFromTdcCount(
    std::uint32_t sector, std::uint32_t wire, std::int32_t count) const {
  if (sector < 1 || sector > readout_.jetSectors().size() || wire < 1 ||
      wire > readout_.jetSectors()[sector - 1].wires.size() || count < 0) {
    throw std::out_of_range("invalid ID jet TDC address");
  }
  constexpr double thirdRfFrequencyMHz = 351.0 / 3.0;
  const auto &calibration =
      readout_.jetSectors()[sector - 1].wires[wire - 1].calibration;
  const auto low = count & 3;
  const auto high = count - low;
  const auto time =
      (high / 4.0 + calibration[17 + low]) * 1000.0 / thirdRfFrequencyMHz;
  return time - readout_.driftTimeZeroNs();
}

double InnerDetectorJetResponse::maximumDriftTimeNs() const {
  double maximum{};
  constexpr auto halfSector = std::numbers::pi / 24.0;
  for (std::uint32_t sector = 1; sector <= readout_.jetSectors().size();
       ++sector) {
    for (std::uint32_t wire = 1;
         wire <= readout_.jetSectors()[sector - 1].wires.size(); ++wire) {
      maximum = std::max(
          {maximum,
           driftTimeNs(sector, wire, InnerDetectorDriftSide::Left, -halfSector),
           driftTimeNs(sector, wire, InnerDetectorDriftSide::Right,
                       halfSector)});
    }
  }
  return maximum;
}

} // namespace delphi_edm4hep::simulation
