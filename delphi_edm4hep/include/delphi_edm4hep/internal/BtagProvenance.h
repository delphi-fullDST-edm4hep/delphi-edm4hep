// Stable machine-readable provenance values for prefix-isolated b-tag output.

#pragma once

#include <string_view>

namespace delphi_edm4hep::btag::provenance {

inline constexpr std::string_view kKeepDelana = "keep-delana";
inline constexpr std::string_view kReplaceWithAabtag = "replace-with-aabtag";
inline constexpr std::string_view kUnknownIflpvt = "unknown-iflpvt";

constexpr std::string_view primaryVertexPolicy(int iflpvt) {
  switch (iflpvt) {
    case 0: return kKeepDelana;
    case 1: return kReplaceWithAabtag;
    default: return kUnknownIflpvt;
  }
}

}  // namespace delphi_edm4hep::btag::provenance
