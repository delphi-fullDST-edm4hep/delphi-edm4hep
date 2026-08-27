#include "delphi_edm4hep/internal/AabtagStatus.h"

#include <cassert>

using delphi_edm4hep::aabtag::eventStatus;

int main() {
  constexpr auto success = eventStatus(0, 0);
  static_assert(success.badEventCode == 0);
  static_assert(success.algorithmInvoked);
  static_assert(success.valid);

  constexpr auto processingFailure = eventStatus(0, 1);
  static_assert(processingFailure.badEventCode == 1);
  static_assert(processingFailure.algorithmInvoked);
  static_assert(!processingFailure.valid);

  constexpr auto vertexFailure = eventStatus(0, 2);
  static_assert(vertexFailure.badEventCode == 2);
  static_assert(vertexFailure.algorithmInvoked);
  static_assert(!vertexFailure.valid);

  // Regression for the PSFBTG bypass: a stale success code remains observable
  // as raw metadata but must never make the skipped current event valid.
  constexpr auto skippedAfterSuccess =
      eventStatus(1, success.badEventCode);
  static_assert(skippedAfterSuccess.badEventCode == 0);
  static_assert(!skippedAfterSuccess.algorithmInvoked);
  static_assert(!skippedAfterSuccess.valid);

  // The same rule holds if the stale snapshot came from a failed event.
  constexpr auto skippedAfterFailure =
      eventStatus(2, vertexFailure.badEventCode);
  static_assert(skippedAfterFailure.badEventCode == 2);
  static_assert(!skippedAfterFailure.algorithmInvoked);
  static_assert(!skippedAfterFailure.valid);

  assert(success.valid);
  return 0;
}
