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
};

// Read the original BTAG bank directly without using PSHBTG or mutating the
// recalculated AABTAG state held by the remaining PSBEG sequence.
void refresh();

const BtagInfo& current();

}  // namespace delphi_edm4hep::btag
