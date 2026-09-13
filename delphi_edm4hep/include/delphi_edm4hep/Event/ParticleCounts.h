#pragma once

#include <cstdint>

namespace delphi_edm4hep::event {

struct ParticleCounts {
  std::int32_t charged = 0;
  std::int32_t neutral = 0;
};

// DELPHI PA.MAIN charge codes are 0 for neutral and non-zero for charged or
// charge-ambiguous particles. Keep that historical classification explicit
// instead of inferring it from EDM4hep's floating-point charge, where code 3
// is intentionally represented as zero.
template <typename Range>
constexpr ParticleCounts countParticleChargeCodes(const Range& chargeCodes) {
  ParticleCounts result;
  for (const auto code : chargeCodes) {
    if (code == 0) {
      ++result.neutral;
    } else {
      ++result.charged;
    }
  }
  return result;
}

}  // namespace delphi_edm4hep::event
