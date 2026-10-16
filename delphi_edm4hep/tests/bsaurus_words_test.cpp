// bsaurus_words_test.cpp — the three BSAURUS word tables must cover every slot
// of their Fortran array exactly once, with a distinct, non-empty name per
// slot. A gap would leave a slot unnamed in the metadata frame; an overlap
// would mislabel one; both would go unnoticed at run time.

#include "delphi_edm4hep/internal/BsaurusCommons.h"
#include "delphi_edm4hep/internal/BsaurusWords.h"

#include <array>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace bs = delphi_edm4hep::bsaurus;
namespace w  = bs::words;

namespace {

template <std::size_t N>
int check(const char* what, const std::array<w::Entry, N>& table, int total)
{
  int failures = 0;
  std::vector<int> hits(static_cast<std::size_t>(total) + 1, 0);
  for (const auto& e : table) {
    if (e.name.empty()) {
      std::printf("%s: table entry with an empty name (array sized too large?)\n", what);
      ++failures;
    }
    if (e.count != 1 && e.count != 3 && e.count != 4 && e.count != 6) {
      std::printf("%s: %.*s has count %d, which expand() cannot name\n", what,
                  static_cast<int>(e.name.size()), e.name.data(), e.count);
      ++failures;
    }
    for (int c = 0; c < e.count; ++c) {
      const int slot = e.first + c;
      if (slot < 1 || slot > total) {
        std::printf("%s: %.*s reaches slot %d outside 1..%d\n", what,
                    static_cast<int>(e.name.size()), e.name.data(), slot, total);
        ++failures;
      } else {
        ++hits[static_cast<std::size_t>(slot)];
      }
    }
  }
  for (int slot = 1; slot <= total; ++slot) {
    if (hits[static_cast<std::size_t>(slot)] != 1) {
      std::printf("%s: slot %d is named %d times\n", what, slot,
                  hits[static_cast<std::size_t>(slot)]);
      ++failures;
    }
  }
  const auto names = w::expand(table, total);
  if (static_cast<int>(names.size()) != total) {
    std::printf("%s: expand() returned %zu names for %d slots\n", what, names.size(), total);
    ++failures;
  }
  std::set<std::string> distinct;
  for (const auto& n : names) {
    if (n.empty()) { std::printf("%s: an expanded name is empty\n", what); ++failures; }
    if (!distinct.insert(n).second) {
      std::printf("%s: duplicate expanded name %s\n", what, n.c_str());
      ++failures;
    }
  }
  return failures;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += check("BSEVT", w::kEvent,      bs::kNEventWords);
  failures += check("BSHEM", w::kHemisphere, bs::kNHemWords);
  failures += check("BSPAR", w::kTrack,      bs::kNTrackWords);
  if (failures) {
    std::printf("bsaurus_words_test: %d failure(s)\n", failures);
    return 1;
  }
  std::puts("bsaurus_words_test: ok");
  return 0;
}
