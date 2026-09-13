#include "delphi_edm4hep/Simulation/TpcReadoutGeometry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>

namespace delphi_edm4hep::simulation {
namespace {

const geometry::CargoRecord &record(const geometry::CargoDatabase &database,
                                    const std::string &path) {
  const geometry::CargoRecord *found{};
  for (const auto &candidate : database.records()) {
    if (candidate.kind == "CALB" && candidate.path == path) {
      if (found != nullptr) {
        throw std::runtime_error("duplicate TPC calibration record: " + path);
      }
      found = &candidate;
    }
  }
  if (found == nullptr) {
    throw std::runtime_error("missing TPC calibration record: " + path);
  }
  return *found;
}

std::vector<double> values(const geometry::CargoRecord &source,
                           std::string_view fieldName) {
  const auto *field = source.findField(fieldName);
  if (field == nullptr) {
    throw std::runtime_error("missing TPC " + std::string(fieldName) +
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
    throw std::runtime_error("invalid TPC field count: " + source.path);
  }
  std::vector<double> result;
  double value{};
  while (input >> value) {
    result.push_back(value);
  }
  if (result.size() != count) {
    throw std::runtime_error("TPC field count mismatch: " + source.path);
  }
  return result;
}

std::string padRowPath(unsigned int row) {
  std::ostringstream path;
  path << "/TPC*/ENP0/SC00.SENS$PR" << std::setw(2) << std::setfill('0') << row
       << ".B";
  return path.str();
}

std::string sectorPath(unsigned int endcap, unsigned int geometrySector) {
  std::ostringstream path;
  path << "/TPC*/ENP" << endcap << "/SC" << std::setw(2) << std::setfill('0')
       << geometrySector << ".B";
  return path.str();
}

} // namespace

TpcReadoutGeometry::TpcReadoutGeometry(std::vector<TpcPadRow> rows,
                                       std::vector<TpcSectorTransform> sectors,
                                       double driftHalfLengthCm)
    : rows_(std::move(rows)), sectors_(std::move(sectors)),
      driftHalfLengthCm_(driftHalfLengthCm) {
  if (rows_.empty() || sectors_.empty() || driftHalfLengthCm_ <= 0) {
    throw std::runtime_error("TPC readout geometry must not be empty");
  }
  for (std::size_t index = 0; index < rows_.size(); ++index) {
    const auto &row = rows_[index];
    if (row.number != index + 1 || row.padCount == 0 || row.radiusCm <= 0 ||
        row.padHeightCm <= 0 || row.padWidthCm <= 0 ||
        (index != 0 && row.radiusCm <= rows_[index - 1].radiusCm)) {
      throw std::runtime_error("invalid TPC pad-row geometry");
    }
  }
}

TpcReadoutGeometry
TpcReadoutGeometry::fromCargo(const geometry::CargoDatabase &database,
                              const geometry::GeometryModel &geometryModel) {
  std::vector<TpcPadRow> rows;
  for (unsigned int rowNumber = 1; rowNumber <= 16; ++rowNumber) {
    const auto path = padRowPath(rowNumber);
    const auto &calibration = record(database, path);
    const auto location = values(calibration, "LOCC");
    const auto size = values(calibration, "SIZC");
    if (location.size() != 6 || size.size() != 4 ||
        std::lround(location[2]) != std::lround(size[1])) {
      throw std::runtime_error("invalid TPC pad-row fields: " +
                               calibration.path);
    }
    rows.push_back({rowNumber,
                    static_cast<unsigned int>(std::lround(location[2])),
                    location[3], size[2], size[3]});
  }

  std::vector<TpcSectorTransform> sectors;
  double driftHalfLengthCm{};
  for (unsigned int endcap = 0; endcap < 2; ++endcap) {
    for (unsigned int slot = 0; slot < 6; ++slot) {
      const auto geometrySector = 2 * slot + endcap;
      const auto *node =
          geometryModel.findNode(sectorPath(endcap, geometrySector));
      if (node == nullptr || node->references.size() != 1) {
        throw std::runtime_error("invalid TPC sector transform");
      }
      if (endcap == 0 && slot == 0) {
        if (node->shapes.size() != 1 || node->shapes.front().tag != "POL6" ||
            node->shapes.front().parameters.size() != 13) {
          throw std::runtime_error("invalid TPC sector drift geometry");
        }
        // STINI assigns ZMAXTP=ABS(SCSHAP(7)); the C++ shape payload omits
        // the Fortran field count, making that value parameters[6].
        driftHalfLengthCm = std::abs(node->shapes.front().parameters[6]);
      }
      const auto readoutSector = endcap == 0 ? slot + 1 : 12 - slot;
      const auto &reference = node->references.front();
      sectors.push_back({readoutSector, geometrySector, endcap,
                         reference.translationCm[0], reference.translationCm[1],
                         reference.rotationDegrees[0]});
    }
  }
  std::sort(sectors.begin(), sectors.end(),
            [](const auto &left, const auto &right) {
              return left.readoutSector < right.readoutSector;
            });
  return {std::move(rows), std::move(sectors), driftHalfLengthCm};
}

std::optional<TpcPadAddress>
TpcReadoutGeometry::locatePad(double xCm, double yCm, double zCm,
                              double rowToleranceCm) const {
  if (rowToleranceCm < 0) {
    throw std::runtime_error("TPC row tolerance must be non-negative");
  }
  const auto endcap = zCm < 0 ? 0U : 1U;
  const TpcSectorTransform *bestSector{};
  double bestPhi{};
  double bestRadius{};
  for (const auto &sector : sectors_) {
    if (sector.endcap != endcap) {
      continue;
    }
    const auto angle = sector.rotationDegrees * std::numbers::pi / 180.0;
    const auto dx = xCm - sector.translationXCm;
    const auto dy = yCm - sector.translationYCm;
    const auto localX = std::cos(angle) * dx + std::sin(angle) * dy;
    const auto localY = -std::sin(angle) * dx + std::cos(angle) * dy;
    const auto phi = std::atan2(localX, localY);
    if (std::abs(phi) <= std::numbers::pi / 6.0 &&
        (bestSector == nullptr || std::abs(phi) < std::abs(bestPhi))) {
      bestSector = &sector;
      bestPhi = phi;
      bestRadius = std::hypot(localX, localY);
    }
  }
  if (bestSector == nullptr) {
    return std::nullopt;
  }
  const auto row =
      std::min_element(rows_.begin(), rows_.end(),
                       [bestRadius](const auto &left, const auto &right) {
                         return std::abs(left.radiusCm - bestRadius) <
                                std::abs(right.radiusCm - bestRadius);
                       });
  const auto residual = bestRadius - row->radiusCm;
  if (std::abs(residual) > rowToleranceCm) {
    return std::nullopt;
  }
  const auto deltaPhi = (std::numbers::pi / 3.0) / row->padCount;
  const auto half = static_cast<int>(row->padCount / 2);
  const auto pad =
      half + static_cast<int>(bestPhi / deltaPhi) + (bestPhi > 0 ? 1 : 0);
  if (pad < 1 || pad > static_cast<int>(row->padCount)) {
    return std::nullopt;
  }
  return TpcPadAddress{endcap,      bestSector->readoutSector,
                       row->number, static_cast<unsigned int>(pad),
                       residual,    bestPhi};
}

std::array<double, 3>
TpcReadoutGeometry::padCenter(const TpcPadAddress &address, double zCm) const {
  const auto row = std::find_if(rows_.begin(), rows_.end(), [&](const auto &entry) {
    return entry.number == address.row;
  });
  const auto sector =
      std::find_if(sectors_.begin(), sectors_.end(), [&](const auto &entry) {
        return entry.readoutSector == address.sector &&
               entry.endcap == address.endcap;
      });
  if (row == rows_.end() || sector == sectors_.end() || address.pad < 1 ||
      address.pad > row->padCount || (zCm < 0 ? 0U : 1U) != address.endcap) {
    throw std::runtime_error("invalid TPC pad address or z coordinate");
  }
  const auto deltaPhi = (std::numbers::pi / 3.0) / row->padCount;
  const auto half = static_cast<int>(row->padCount / 2);
  const auto phi = (static_cast<int>(address.pad) - half) * deltaPhi -
                   0.5 * deltaPhi;
  const auto localX = row->radiusCm * std::sin(phi);
  const auto localY = row->radiusCm * std::cos(phi);
  const auto angle = sector->rotationDegrees * std::numbers::pi / 180.0;
  return {sector->translationXCm + std::cos(angle) * localX -
              std::sin(angle) * localY,
          sector->translationYCm + std::sin(angle) * localX +
              std::cos(angle) * localY,
          zCm};
}

std::uint64_t TpcReadoutGeometry::encodeCellId(const TpcPadAddress &address) {
  if (address.pad > 255 || address.row > 31 || address.sector > 15 ||
      address.endcap > 1) {
    throw std::runtime_error("TPC pad address exceeds cell-ID bit fields");
  }
  return address.pad | (static_cast<std::uint64_t>(address.row) << 8U) |
         (static_cast<std::uint64_t>(address.sector) << 13U) |
         (static_cast<std::uint64_t>(address.endcap) << 17U);
}

TpcPadAddress TpcReadoutGeometry::decodeCellId(std::uint64_t cellId) {
  if ((cellId >> 18U) != 0) {
    throw std::runtime_error("TPC cell ID has unknown high bits");
  }
  TpcPadAddress address;
  address.pad = static_cast<unsigned int>(cellId & 0xffU);
  address.row = static_cast<unsigned int>((cellId >> 8U) & 0x1fU);
  address.sector = static_cast<unsigned int>((cellId >> 13U) & 0xfU);
  address.endcap = static_cast<unsigned int>((cellId >> 17U) & 0x1U);
  if (address.pad == 0 || address.row == 0 || address.sector == 0) {
    throw std::runtime_error("TPC cell ID has a zero address field");
  }
  return address;
}

} // namespace delphi_edm4hep::simulation
