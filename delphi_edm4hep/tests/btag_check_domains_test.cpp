#include "delphi_edm4hep/internal/BtagCheckDomains.h"

#include <cassert>
#include <cstdint>
#include <limits>

namespace check = delphi_edm4hep::btag::check;

int main() {
  assert(check::isSelectedPayloadName("sDST_AABTAG_Valid", "sDST"));
  assert(check::isSelectedPayloadName("fDST_BTG_ProbAllIP", "fDST"));
  assert(!check::isSelectedPayloadName("sDST_BTAGCFG_Mode", "sDST"));
  assert(!check::isSelectedPayloadName("fDST_AABTAG_Valid", "sDST"));

  assert(check::isPositiveFinite(1.f));
  assert(!check::isPositiveFinite(0.f));
  assert(!check::isPositiveFinite(
      std::numeric_limits<float>::quiet_NaN()));
  assert(check::isNonnegativeFinite(0.f));
  assert(!check::isNonnegativeFinite(-1.f));
  assert(!check::isNonnegativeFinite(
      std::numeric_limits<float>::infinity()));

  assert(check::isSensiblePrimaryVertexNdf(0));
  assert(check::isSensiblePrimaryVertexNdf(200));
  assert(!check::isSensiblePrimaryVertexNdf(-1));
  assert(!check::isSensiblePrimaryVertexNdf(201));

  assert(check::isValidUsedForTag(0));
  assert(check::isValidUsedForTag(201));
  assert(!check::isValidUsedForTag(-1));
  assert(check::isValidAttachedFlag(0));
  assert(check::isValidAttachedFlag(1));
  assert(!check::isValidAttachedFlag(2));

  assert(check::isValidSignedCount(-check::kMaxVdHits,
                                   check::kMaxVdHits));
  assert(!check::isValidSignedCount(check::kMaxVdHits + 1,
                                    check::kMaxVdHits));
  assert(check::isValidSignedCount(-check::kMaxVdLayers,
                                   check::kMaxVdLayers));
  assert(!check::isValidSignedCount(
      std::numeric_limits<std::int32_t>::min(), check::kMaxVdLayers));
}
