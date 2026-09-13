#include "delphi_edm4hep/Simulation/TpcReadoutGeometry.h"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main() {
  using delphi_edm4hep::simulation::TpcPadRow;
  using delphi_edm4hep::simulation::TpcReadoutGeometry;
  using delphi_edm4hep::simulation::TpcSectorTransform;

  TpcReadoutGeometry geometry(
      std::vector<TpcPadRow>{{1, 64, 36.5, 0.959, 0.547},
                             {2, 80, 41.175, 0.842, 0.624}},
      std::vector<TpcSectorTransform>{{1, 0, 0, 0, 0, 0},
                                      {12, 1, 1, 0, 0, 0}},
      145.0);

  const auto onAxis = geometry.locatePad(0, 36.5, -10);
  require(onAxis && onAxis->sector == 1 && onAxis->row == 1 &&
              onAxis->pad == 32,
          "STAMPA zero-boundary pad rule was not preserved");

  const auto deltaPhi = (std::numbers::pi / 3.0) / 64.0;
  const auto positive = geometry.locatePad(
      36.5 * std::sin(deltaPhi * 0.25), 36.5 * std::cos(deltaPhi * 0.25), -10);
  require(positive && positive->pad == 33,
          "positive pad boundary was not preserved");
  const auto negative = geometry.locatePad(
      -36.5 * std::sin(deltaPhi * 0.25), 36.5 * std::cos(deltaPhi * 0.25), -10);
  require(negative && negative->pad == 32,
          "negative pad boundary was not preserved");
  require(!geometry.locatePad(0, 39.0, -10),
          "hit outside the one-centimetre row window was accepted");
  require(TpcReadoutGeometry::encodeCellId(*positive) ==
              (33U | (1U << 8U) | (1U << 13U)),
          "TPC cell-ID encoding changed");
  const auto decoded = TpcReadoutGeometry::decodeCellId(
      TpcReadoutGeometry::encodeCellId(*positive));
  require(decoded.endcap == positive->endcap &&
              decoded.sector == positive->sector && decoded.row == positive->row &&
              decoded.pad == positive->pad,
          "TPC cell-ID round trip changed");
  const auto centre = geometry.padCenter({0, 1, 1, 32, 0, 0}, -10.0);
  const auto roundTrip = geometry.locatePad(centre[0], centre[1], centre[2]);
  require(roundTrip && roundTrip->pad == 32 && roundTrip->row == 1 &&
              roundTrip->sector == 1,
          "TPC pad-centre round trip changed");
}
