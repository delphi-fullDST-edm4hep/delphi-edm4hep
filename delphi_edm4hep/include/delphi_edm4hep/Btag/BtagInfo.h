#pragma once

#include <array>

namespace delphi_edm4hep::btag {

struct EventLevelTag {
  std::array<float, 3> probabilityNegative{{2.f, 2.f, 2.f}};
  std::array<float, 3> probabilityPositive{{2.f, 2.f, 2.f}};
  std::array<float, 3> probabilityAll{{2.f, 2.f, 2.f}};
  std::array<float, 3> thrustAxis{{2.f, 2.f, 2.f}};
  float thrustValue = 2.f;
};

struct BtagInfo {
  EventLevelTag stored;
  EventLevelTag recalculated;
  bool algorithmInvoked = false;
};

// Set the one process-global AABTAG name default expected before AADATA.
// This is deliberately separate from initializeRecalculation(): the latter
// must run only after the first event has supplied DSTQID state.
void initialize();

// Read the original BTAG bank directly without using PSHBTG.
void refresh();

// Recalculate AABTAG directly from the current event and converter-owned
// beamspot. This is the PSFBTG contract without its SKELANA common block.
void recalculate();

// Validation-oracle adapter: preserve the result produced by optional PSBEG
// reference binaries without exposing the legacy common to writers.
void setLegacyRecalculated(const EventLevelTag& tag, bool algorithmInvoked);

const BtagInfo& current();

}  // namespace delphi_edm4hep::btag
