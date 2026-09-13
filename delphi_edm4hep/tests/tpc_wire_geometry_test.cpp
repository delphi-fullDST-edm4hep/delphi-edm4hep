#include "delphi_edm4hep/Simulation/TpcWireGeometry.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

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
  using delphi_edm4hep::simulation::TpcWireGeometry;

  TpcReadoutGeometry readout(
      std::vector<TpcPadRow>{{1, 64, 36.5, 0.959, 0.547}},
      std::vector<TpcSectorTransform>{{1, 0, 0, 0, 0, 0}, {12, 1, 1, 0, 0, 0}},
      145.0);
  TpcWireGeometry wires(readout, 192, 0.4, 31.05, 160, 54.54, -1.1);

  require(std::abs(wires.wireReferenceCm() - 30.85) < 1e-12,
          "DWZTPC convention changed");
  require(std::abs(wires.wireRadiusCm(192) - 107.45) < 1e-12,
          "last wire radius changed");
  const auto first = wires.locate(0, 31.05, -10, 0, 1);
  require(first && first->endcap == 0 && first->sector == 1 &&
              first->wire == 1 && first->outwardDirection == 1,
          "first wire was not located");
  const auto second = wires.locate(0, 31.26, -10, 0, -1);
  require(second && second->wire == 2 && second->outwardDirection == -1,
          "STINTR wire boundary or direction changed");
  require(!wires.locate(54.0, wires.wireRadiusCm(160), -10, 0, 1),
          "high-wire tapered dead area was accepted");
  const auto centre = wires.wirePoint(*first);
  require(std::abs(centre[0]) < 1e-12 && std::abs(centre[1] - 31.05) < 1e-12 &&
              std::abs(centre[2] + 10.0) < 1e-12,
          "wire-centre transform changed");
}
