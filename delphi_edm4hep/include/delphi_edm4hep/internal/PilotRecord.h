#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace delphi_edm4hep::pilot {

extern "C" {
struct PxchdrCommon {
  int length;
  int words[1024];
};
extern PxchdrCommon pxchdr_;
}

static_assert(sizeof(PxchdrCommon) == 4100,
              "PXCHDR layout mismatch (expected 0x1004 bytes)");

// Fortran-compatible one-based access to IPILOT.
inline int word(int oneBasedIndex) {
  if (oneBasedIndex < 1 || oneBasedIndex > pxchdr_.length ||
      oneBasedIndex > static_cast<int>(std::size(pxchdr_.words))) {
    throw std::out_of_range("DELPHI pilot word index " +
                            std::to_string(oneBasedIndex));
  }
  return pxchdr_.words[oneBasedIndex - 1];
}

}  // namespace delphi_edm4hep::pilot
