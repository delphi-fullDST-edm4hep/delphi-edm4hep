// BeamSpotStatus — implementation of the database check.

#include "delphi_edm4hep/internal/BeamSpotStatus.h"

#include "delphi_edm4hep/Event/EventInfo.h"

#include "phdst/phciii.hpp"

#include <stdexcept>
#include <string>

namespace delphi_edm4hep::beamspot {

void requireDatabase() {
  const auto& sigma = event::current().beamSpot.sigmaCm;
  if (sigma[0] == 0.f && sigma[1] == 0.f && sigma[2] == 0.f) {
    throw std::runtime_error(
        "beam-spot database was not read: zero widths (run " +
        std::to_string(phdst::IIIRUN) + ", event " +
        std::to_string(phdst::IIIEVT) + ')');
  }
}

}  // namespace delphi_edm4hep::beamspot
