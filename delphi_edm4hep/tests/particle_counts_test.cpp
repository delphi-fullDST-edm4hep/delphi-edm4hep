#include "delphi_edm4hep/Event/ParticleCounts.h"

#include <array>
#include <cstdint>

int main() {
  using delphi_edm4hep::event::countParticleChargeCodes;

  constexpr std::array<std::int32_t, 0> empty{};
  constexpr auto emptyCounts = countParticleChargeCodes(empty);
  static_assert(emptyCounts.charged == 0);
  static_assert(emptyCounts.neutral == 0);

  // Code 3 is charge-ambiguous but belongs to DELPHI's charged branch.
  constexpr std::array<std::int32_t, 6> mixed{{0, 1, 2, 3, 0, -1}};
  constexpr auto mixedCounts = countParticleChargeCodes(mixed);
  static_assert(mixedCounts.charged == 4);
  static_assert(mixedCounts.neutral == 2);
}
