#include "delphi_edm4hep/internal/AabtagTrackState.h"

#include "delphi_edm4hep/Event/EventInfo.h"
#include "delphi_edm4hep/internal/AabtagCommons.h"
#include "delphi_edm4hep/internal/AabtagStatus.h"

#include <algorithm>
#include <array>
#include <limits>

namespace delphi_edm4hep::aabtag {

namespace {
constexpr double kCm2Mm = 10.0;
constexpr float  kNotMeasured = std::numeric_limits<float>::quiet_NaN();
}  // namespace

std::unordered_map<int, int> lpaToTrack() {
  std::unordered_map<int, int> out;
  const auto status = eventStatus(event::current().beamSpot.errorCode, IBAD());
  if (!status.valid || !status.algorithmInvoked) return out;
  const int ntrk = std::clamp(NTRK(), 0, kMaxTracks);
  for (int i = 1; i <= ntrk; ++i) out.emplace(IADTR(i), i);
  return out;
}

edm4hep::TrackState vertexState(int i) {
  edm4hep::TrackState st{};
  st.location = edm4hep::TrackState::AtVertex;
  // D0 is negated into the EDM4hep convention, as the perigee state is
  // (Helix::fromPerigee); AABTAG stores the DELPHI sign. Z0 is not.
  st.D0        = static_cast<float>(-PARIMP(i) * kCm2Mm);
  st.Z0        = static_cast<float>( EZED  (i) * kCm2Mm);
  st.phi       = kNotMeasured;
  st.omega     = kNotMeasured;
  st.tanLambda = kNotMeasured;
  st.time      = kNotMeasured;
  st.referencePoint = {static_cast<float>(POSVX(1) * kCm2Mm),
                       static_cast<float>(POSVX(2) * kCm2Mm),
                       static_cast<float>(POSVX(3) * kCm2Mm)};
  // Only D0 and Z0 are measured; the rest stay NaN rather than zero, which
  // would claim a measurement that was never made.
  std::array<float, 21> cov;
  cov.fill(kNotMeasured);
  const float dD0 = static_cast<float>(SIGIMP(i) * kCm2Mm);
  const float dZ0 = static_cast<float>(SIGZED(i) * kCm2Mm);
  cov[0] = dD0 * dD0;   // (D0, D0)
  cov[9] = dZ0 * dZ0;   // (Z0, Z0)
  st.covMatrix = cov;
  return st;
}

}  // namespace delphi_edm4hep::aabtag
