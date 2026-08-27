// BtagCheckDomains.h -- pure domain predicates used by delphi_btag_check.
//
// Keep these independent of podio/EDM4hep so the corruption boundaries can be
// unit-tested without opening a ROOT file.

#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace delphi_edm4hep::btag::check {

inline constexpr int kPrimaryVertexAlgorithmType = 3;
inline constexpr int kMaxTracks = 100;
inline constexpr int kMaxPrimaryVertexNdf = 2 * kMaxTracks;
inline constexpr std::int32_t kMaxVdHits = 6;
inline constexpr std::int32_t kMaxVdLayers = 3;

inline bool isSelectedPayloadName(std::string_view name,
                                  std::string_view source) {
  if (!name.starts_with(source)) return false;
  name.remove_prefix(source.size());
  return name.starts_with("_BTG_") || name.starts_with("_AABTAG_");
}

inline bool isFinite(float value) { return std::isfinite(value); }
inline bool isFinite(double value) { return std::isfinite(value); }

inline bool isPositiveFinite(float value) {
  return isFinite(value) && value > 0.f;
}

inline bool isNonnegativeFinite(float value) {
  return isFinite(value) && value >= 0.f;
}

inline bool isSensiblePrimaryVertexNdf(int value) {
  return value >= 0 && value <= kMaxPrimaryVertexNdf;
}

// ISRT is a nonnegative DELPHI category code, not a boolean. Values such as
// 101/201 are present in valid v94c output.
inline bool isValidUsedForTag(std::int32_t value) { return value >= 0; }

inline bool isValidAttachedFlag(std::int32_t value) {
  return value == 0 || value == 1;
}

// The legacy VD count arrays may be negated to mark a rejected track. Avoid
// abs(INT_MIN) overflow by widening before taking the magnitude.
inline bool isValidSignedCount(std::int32_t value, std::int32_t maximum) {
  const auto wide = static_cast<std::int64_t>(value);
  const auto magnitude = wide < 0 ? -wide : wide;
  return magnitude <= static_cast<std::int64_t>(maximum);
}

}  // namespace delphi_edm4hep::btag::check
