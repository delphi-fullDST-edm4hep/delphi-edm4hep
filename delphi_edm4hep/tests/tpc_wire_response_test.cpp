#include "delphi_edm4hep/Simulation/TpcWireResponse.h"

#include <cmath>
#include <numeric>
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
  using delphi_edm4hep::simulation::TpcWireResponse;

  TpcReadoutGeometry readout(
      std::vector<TpcPadRow>{{1, 64, 36.5, 0.959, 0.547}},
      std::vector<TpcSectorTransform>{{1, 0, 0, 0, 0, 0}, {12, 1, 1, 0, 0, 0}},
      145.0);
  TpcWireResponse response(
      TpcWireGeometry(readout, 192, 0.4, 31.05, 160, 54.54, -1.1));

  const auto coefficient =
      response.transverseDiffusionCoefficient(1.2312434, 7.0, 25306, 145);
  require(coefficient > 0.009 && coefficient < 0.011,
          "field-dependent transverse diffusion changed");
  const auto charges =
      response.distribute(0, response.geometry().wireRadiusCm(20), 45, 0, 1,
                          1000, 1.2312434, 7.0, 25306, 145);
  require(charges.size() == 3 && charges[0].address.wire == 19 &&
              charges[1].address.wire == 20 && charges[2].address.wire == 21,
          "adjacent-wire ordering changed");
  require(charges[0].electrons == charges[2].electrons &&
              charges[0].electrons > 40 && charges[0].electrons < 50,
          "STDEDX leakage population changed");
  const auto total = std::accumulate(charges.begin(), charges.end(), 0U,
                                     [](unsigned int sum, const auto &charge) {
                                       return sum + charge.electrons;
                                     });
  require(total == 1000, "wire response did not conserve charge");

  const auto inward =
      response.distribute(0, response.geometry().wireRadiusCm(20), 45, 0, -1,
                          1000, 1.2312434, 7.0, 25306, 145);
  require(inward.size() == 3 && inward[0].address.wire == 21 &&
              inward[2].address.wire == 19,
          "inward wire-leakage direction changed");
}
