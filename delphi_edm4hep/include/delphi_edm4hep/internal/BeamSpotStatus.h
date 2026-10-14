// BeamSpotStatus.h -- whether the beam spot describes the current event.
//
// VDBSPT looks the beam spot up in the per-processing database under
// $DELPHI_DAT (e.g. 95C2.DB), by run and file; EventInfo calls it once per
// event and keeps the result. Its error code reports the outcome
// (vdbeam.car:582-651):
//   0  the entry for this run and file;
//   1  no entry for this file; the preceding entry is used instead;
//   2  no usable entry. The position is then whatever row the neighbour search
//      stopped on -- another run's beam spot, or an out-of-range read that can
//      give (0,0,0) -- so it describes nothing.
// If the database file cannot be opened, VDBSPT returns on the first call
// without setting anything (vdbeam.car:1167-1171) and reports 2 afterwards, so
// a missing file would otherwise pass as a run of code-2 events.

#pragma once

namespace delphi_edm4hep::beamspot {

enum ErrorCode : int { kOwnEntry = 0, kPrecedingEntry = 1, kNoEntry = 2 };

// True when the position and widths describe a measured beam spot and may be
// written; otherwise they are written as NaN.
constexpr bool positionUsable(int errorCode) {
  return errorCode == kOwnEntry || errorCode == kPrecedingEntry;
}

// Refuse the event when the database was never read. Every lookup that reads
// the file writes non-zero widths, code 2 included, because the beam size is
// folded into them (SETBS, vdbeam.car:656-672). EventInfo clears the widths
// before calling VDBSPT, so all three zero can only be the unopened-file path.
// The error code does not show it: SKELANA's PSBEAM still calls VDBSPT first
// in each event, so EventInfo's call already gets the 2 of the later calls.
// Both passes call this first thing for every event.
void requireDatabase();

}  // namespace delphi_edm4hep::beamspot
