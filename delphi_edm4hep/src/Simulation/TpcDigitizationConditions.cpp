#include "delphi_edm4hep/Simulation/TpcDigitizationConditions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace delphi_edm4hep::simulation {
namespace {

const geometry::CargoRecord &record(const geometry::CargoDatabase &database,
                                    std::string_view path) {
  const geometry::CargoRecord *found{};
  for (const auto &candidate : database.records()) {
    if (candidate.kind == "CALB" && candidate.path == path) {
      if (found != nullptr) {
        throw std::runtime_error("duplicate TPC conditions record: " +
                                 std::string(path));
      }
      found = &candidate;
    }
  }
  if (found == nullptr) {
    throw std::runtime_error("missing TPC conditions record: " +
                             std::string(path));
  }
  return *found;
}

std::vector<double> values(const geometry::CargoRecord &source,
                           std::string_view fieldName) {
  const auto *field = source.findField(fieldName);
  if (field == nullptr) {
    throw std::runtime_error("missing TPC conditions field " +
                             std::string(fieldName) + ": " + source.path);
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
    throw std::runtime_error("invalid TPC conditions field count: " +
                             source.path);
  }
  std::vector<double> result;
  double value{};
  while (input >> value) {
    result.push_back(value);
  }
  if (result.size() != count) {
    throw std::runtime_error("TPC conditions field count mismatch: " +
                             source.path);
  }
  return result;
}

std::string padRowPath(unsigned int endcap, unsigned int geometrySector,
                       unsigned int row) {
  std::ostringstream path;
  path << "/TPC*/ENP" << endcap << "/SC" << std::setw(2)
       << std::setfill('0') << geometrySector << ".SENS$PR" << std::setw(2)
       << row << ".B";
  return path.str();
}

std::uint32_t packedWord(double value, const std::string &path) {
  if (value < 0 || value > 4294967295.0 || value != std::floor(value)) {
    throw std::runtime_error("invalid packed TPC calibration word: " + path);
  }
  return static_cast<std::uint32_t>(value);
}

std::uint64_t padKey(unsigned int readoutSector, unsigned int row,
                     unsigned int pad) {
  return (static_cast<std::uint64_t>(readoutSector) << 16U) |
         (static_cast<std::uint64_t>(row) << 8U) | pad;
}

} // namespace

TpcDigitizationConditions TpcDigitizationConditions::fromCargo(
    const geometry::CargoDatabase &database,
    const TpcReadoutGeometry &readout) {
  const auto &global = record(database, "/TPC*.B");
  const auto slow = values(global, "SLOW");
  const auto user = values(global, "USER");
  const auto status = values(global, "ISLO");
  if (slow.size() != 18 || user.size() != 16 || status.size() != 16) {
    throw std::runtime_error("invalid global TPC digitization conditions");
  }

  TpcDigitizationConditions result;
  result.highVoltageVolt_ = slow[5] * 1000.0;
  result.minimumIonizingDedx_ = user[0];
  result.meanPadAmplitude_ = user[13];
  if (result.highVoltageVolt_ <= 0 || result.minimumIonizingDedx_ <= 0 ||
      result.meanPadAmplitude_ <= 0) {
    throw std::runtime_error("non-positive global TPC conditions");
  }

  std::array<double, 2> driftVelocity{};
  for (unsigned int endcap = 0; endcap < 2; ++endcap) {
    const auto endcapPath =
        std::string("/TPC*/ENP") + std::to_string(endcap) + ".B";
    const auto endcapSlow = values(record(database, endcapPath), "SLOW");
    if (endcapSlow.size() != 14 || endcapSlow[0] <= 0) {
      throw std::runtime_error("invalid TPC endcap drift velocity: " +
                               endcapPath);
    }
    driftVelocity[endcap] = endcapSlow[0];
  }

  const auto gateWord = static_cast<std::uint32_t>(std::llround(status[6]));
  result.sectors_.reserve(readout.sectors().size());
  for (const auto &sector : readout.sectors()) {
    if (sector.geometrySector >= 12 || sector.endcap >= driftVelocity.size()) {
      throw std::runtime_error("TPC sector exceeds packed conditions fields");
    }
    const auto raw = (gateWord >> (2U * sector.geometrySector)) & 0x3U;
    auto decoded = static_cast<int>(raw) - 1;
    // Preserve STCALB's recovery for invalid two-bit database values.
    if (decoded < 0 || decoded >= 2) {
      decoded = decoded == 2 ? 1 : 0;
    }
    result.sectors_.push_back(
        {sector.readoutSector, sector.geometrySector, sector.endcap,
         driftVelocity[sector.endcap], decoded == 1});
  }

  constexpr std::array<int, 16> rowOffsets{
      1519, 1455, 1151, 255, 687, 1583, 1231, 591,
      143,  767,  479,  1327, 879, -1,   1007, 335};
  const auto averagePadSignalPerCount =
      0.00215 * 740.3 / result.meanPadAmplitude_;
  for (const auto &sector : readout.sectors()) {
    const auto &sectorConditions = result.sector(sector.readoutSector);
    std::array<bool, 1680> seenChannels{};
    for (const auto &row : readout.rows()) {
      const auto path = padRowPath(sector.endcap, sector.geometrySector,
                                   row.number);
      const auto &calibration = record(database, path);
      const auto lead = values(calibration, "LEAD");
      const auto packed = values(calibration, "CALP");
      const auto statuses = values(calibration, "STAT");
      if (lead.size() != 12 || packed.size() != 2 * row.padCount ||
          statuses.empty() ||
          std::llround(statuses.front()) !=
              static_cast<long long>(statuses.size() - 1)) {
        throw std::runtime_error("invalid TPC pad calibration: " + path);
      }
      auto scale = lead[7];
      if (scale <= 900) {
        scale = 100.0;
      }
      std::map<unsigned int, unsigned int> channelStatuses;
      for (std::size_t index = 1; index < statuses.size(); ++index) {
        const auto encoded = static_cast<unsigned int>(
            std::llround(statuses[index]));
        channelStatuses[encoded / 100U] = encoded % 100U;
      }

      for (unsigned int pad = 1; pad <= row.padCount; ++pad) {
        const auto electronicsChannel =
            static_cast<unsigned int>(rowOffsets[row.number - 1] +
                                      static_cast<int>(pad));
        if (electronicsChannel >= seenChannels.size() ||
            seenChannels[electronicsChannel]) {
          throw std::runtime_error("invalid TPC row-to-channel map: " + path);
        }
        seenChannels[electronicsChannel] = true;
        const auto firstWord = packedWord(packed[2 * (pad - 1)], path);
        const auto secondWord = packedWord(packed[2 * (pad - 1) + 1], path);
        const auto pedestal = static_cast<double>(firstWord & 0xffffU) / scale;
        auto lowSlope = static_cast<double>((firstWord >> 16U) & 0xffffU) /
                        scale / 6.0 * averagePadSignalPerCount;
        if (sectorConditions.gateClosed) {
          lowSlope *= 0.85;
        }
        auto ratio = static_cast<double>((secondWord >> 16U) & 0xffffU) /
                     scale;
        if (ratio >= 0.493 && ratio <= 0.495) {
          ratio = 4.94;
        }
        if (lowSlope <= 0 || ratio <= 0 || std::abs(ratio - 1.0) < 1e-12) {
          throw std::runtime_error("invalid TPC FADC calibration: " + path);
        }
        const auto highSlope = lowSlope * ratio;
        const auto highPedestal = 192.0 * (1.0 - 1.0 / ratio) +
                                  pedestal / ratio;
        const auto rangeBreak = lowSlope * highSlope *
                                (highPedestal - pedestal) /
                                (highSlope - lowSlope);
        const auto statusEntry = channelStatuses.find(electronicsChannel);
        result.pads_.push_back(
            {sector.readoutSector,
             sector.geometrySector,
             sector.endcap,
             row.number,
             pad,
             electronicsChannel,
             statusEntry == channelStatuses.end() ? 0U : statusEntry->second,
             pedestal,
             lowSlope,
             highSlope,
             highPedestal,
             ratio,
             rangeBreak});
        if (!result.padIndices_
                 .emplace(padKey(sector.readoutSector, row.number, pad),
                          result.pads_.size() - 1)
                 .second) {
          throw std::runtime_error("duplicate TPC pad calibration: " + path);
        }
      }
    }
  }
  return result;
}

const TpcSectorConditions &
TpcDigitizationConditions::sector(unsigned int readoutSector) const {
  const auto found =
      std::find_if(sectors_.begin(), sectors_.end(), [&](const auto &entry) {
        return entry.readoutSector == readoutSector;
      });
  if (found == sectors_.end()) {
    throw std::runtime_error("unknown TPC readout sector");
  }
  return *found;
}

const TpcPadElectronicsCalibration &
TpcDigitizationConditions::pad(unsigned int readoutSector, unsigned int row,
                               unsigned int padNumber) const {
  const auto found = padIndices_.find(padKey(readoutSector, row, padNumber));
  if (found == padIndices_.end()) {
    throw std::runtime_error("unknown TPC pad calibration");
  }
  return pads_.at(found->second);
}

} // namespace delphi_edm4hep::simulation
