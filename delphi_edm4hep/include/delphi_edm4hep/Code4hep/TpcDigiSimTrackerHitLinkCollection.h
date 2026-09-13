#pragma once

#include "edm4hep/SimTrackerHit.h"
#include "edm4hep/TimeSeries.h"
#include "podio/LinkCollection.h"

namespace delphi_edm4hep {

using TpcDigiSimTrackerHitLinkCollection =
    podio::LinkCollection<edm4hep::TimeSeries, edm4hep::SimTrackerHit>;

} // namespace delphi_edm4hep
