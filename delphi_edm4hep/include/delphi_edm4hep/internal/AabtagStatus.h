// AabtagStatus.h -- pure current-event validity contract for AABTAG output.

#pragma once

namespace delphi_edm4hep::aabtag {

// PSFBTG calls AABTGS only when IERRBS == 0.  If it skips the call, the
// AAFLAG/IBAD common retains a snapshot from an earlier event and therefore
// cannot by itself establish validity for the current event.  Keep that raw
// value for diagnostics, but carry invocation and combined validity separately.
struct EventStatus {
  int  badEventCode;
  bool algorithmInvoked;
  bool valid;
};

constexpr EventStatus eventStatus(int beamSpotErrorCode, int rawBadEventCode) {
  const bool invoked = beamSpotErrorCode == 0;
  return {rawBadEventCode, invoked, invoked && rawBadEventCode == 0};
}

}  // namespace delphi_edm4hep::aabtag
