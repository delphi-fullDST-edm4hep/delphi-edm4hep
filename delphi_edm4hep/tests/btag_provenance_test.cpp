#include "delphi_edm4hep/internal/BtagProvenance.h"

#include <cassert>

int main() {
  using namespace delphi_edm4hep::btag::provenance;
  static_assert(primaryVertexPolicy(0) == kKeepDelana);
  static_assert(primaryVertexPolicy(1) == kReplaceWithAabtag);
  static_assert(primaryVertexPolicy(-1) == kUnknownIflpvt);
  static_assert(primaryVertexPolicy(2) == kUnknownIflpvt);
  assert(kKeepDelana != kReplaceWithAabtag);
  return 0;
}
