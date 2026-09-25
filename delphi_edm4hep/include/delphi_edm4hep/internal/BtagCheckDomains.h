// BtagCheckDomains.h -- pure domain predicates used by delphi_btag_check.
//
// Keep these independent of podio/EDM4hep so the corruption boundaries can be
// unit-tested without opening a ROOT file.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace delphi_edm4hep::btag::check {

inline constexpr int kPrimaryVertexAlgorithmType = 3;
inline constexpr int kMaxTracks = 100;
inline constexpr int kMaxPrimaryVertexNdf = 2 * kMaxTracks;
inline constexpr std::int32_t kMaxVdHits = 6;
inline constexpr std::int32_t kMaxVdLayers = 3;

/// Slot layout of the `AABTAG_TrackTag` `parameters` VectorMember.
///
/// EDM4hep types `parameters` as a bare float vector, so slot meanings and
/// units are convention: declared once here, appended in this order by
/// `Btag.cpp`, read by name by `delphi_btag_check`, with `kTrCount` keeping
/// the two in step. Slots [0..10] come from AABTGS / AASIGN, [11..19] from
/// AAJETS / AASCND / AAJESV; the DELPHI mnemonic locates each in
/// `aabtagxx.car`. Unmarked slots are dimensionless.
enum TrackTagIndex : std::size_t {
  kTrProb = 0,   ///< `TRPR` lifetime probability
  kTrProbZ,      ///< `TRPRZ` the same, using z
  kTrChi2Vd,     ///< `CHI2VD` chi2 of the VD hits on the track
  kTrChi2Tr,     ///< `CHI2TR` track-to-vertex chi2; NaN unless #kTrAttached
  kTrMomentum,   ///< `PMOM` track momentum, GeV
  kTrNVdp,       ///< `NVDP` VD hits
  kTrNVdpz,      ///< `NVDPZ` of which carry z
  kTrNLay,       ///< `NLAY` VD layers touched
  kTrNLayz,      ///< `NLAYZ` of which carry z
  kTrIsrt,       ///< `ISRT` 0 if the track was not used by the tag
  kTrAttached,   ///< 1 if AABTAG attached the track to its primary vertex
  kTrIjet,       ///< `IJET` `AABTAG_CombinedTagRow` row, 1-based
  kTrIthr,       ///< `ITHR` thrust hemisphere (1 or 2)
  kTrPhiv,       ///< `PHIV` signed distance along the jet axis, cm -- not mm
                 ///< like the rest; exactly +-1 wherever #kTrDistj is NaN,
                 ///< carrying only a sign there
  kTrRpdt,       ///< `RPDT` track rapidity with respect to its jet
  kTrDistj,      ///< `DISTJ` 3-D track-jet distance, mm; NaN if not computed
  kTrErrtj,      ///< `ERRTJ` error on #kTrDistj, mm; NaN on the same rows
  kTrInsv,       ///< `INSV` SV hypothesis using the track (+100 flags)
  kTrIst,        ///< `IST` track-quality code (AASTRK / AASLCT / AAIMPC)
  kTrIjsv,       ///< `IJSV` jet after the SV redefinition
  kTrCount       ///< number of slots; the expected `parameters` size
};

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
