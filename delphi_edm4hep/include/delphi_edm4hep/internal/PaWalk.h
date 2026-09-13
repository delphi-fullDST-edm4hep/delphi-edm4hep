// PaWalk.h — internal header (not exported).
//
// Helpers for walking the per-event PA chain (LDTOP-1 -> per-PV -> per-PA)
// and looking up named PA sub-banks via the DELPHI LPHPA Fortran function.
// Used by Tracking, Calorimeter, ParticleId — wherever bank-level reads
// are needed.
//
// Intentionally NOT in include/delphi_edm4hep/: these are implementation
// details that touch the PHDST commons directly. Public callers go
// through the per-domain entry points.

#pragma once

#include "phdst/functions.hpp"
#include "phdst/uxcom.hpp"
#include "phdst/uxlink.hpp"

#include <cmath>
#include <cstring>

namespace phdst {
  // Fortran "integer function iphreq(nump)". Returns auxiliary info
  // (typically the length of the last-resolved PA sub-bank) set by the
  // most recent LPHPA/LPHPAN call. NUMP=1 → bank length in words.
  extern "C" int iphreq_(int* nump);
}

namespace delphi_edm4hep::pawalk {

// DELPHI LPHPA wrapper: get the L-address of a named PA sub-bank.
// Returns 0 if the sub-bank isn't present in this PA. `nump` defaults
// to 0 (the standard "first occurrence" lookup that matches the
// long-form `Q(LPHPA('NAME', lpa) + N)` idiom in the legacy converter).
inline int lphpa(const char* iddp, int lpa, int nump = 0) {
  int lpa_arg  = lpa;
  int nump_arg = nump;
  return phdst::lphpa_(iddp, &lpa_arg, &nump_arg, std::strlen(iddp));
}

// IPHREQ wrapper: bank length of the most-recent LPHPA result (in
// words). Pass nump=1 for the standard length query.
inline int iphreq(int nump = 1) {
  int n = nump;
  return phdst::iphreq_(&n);
}

// Charge sign in the convention Helix expects, from PA.MAIN word +8 (the
// DELPHI charge code: 1 positive, 2 negative). DELPHI's curvature sign is
// opposite to the charge, so the code is negated here; 0 for neutral or
// undefined. Shared by the writers that build track states from PA banks.
inline int conversionCharge(int lpa) {
  const int lmain = lphpa("MAIN", lpa);
  if (lmain <= 0) return 0;
  const int code = static_cast<int>(std::lround(phdst::Q(lmain + 8)));
  if (code == 1) return -1;
  if (code == 2) return +1;
  return 0;
}

// Walk every PA in the current event. Calls `fn(lpa, paIdx)` for each
// PA where `lpa` is the ZEBRA L-address of the PA bank and `paIdx` is
// a 0-based running counter across all PVs (does NOT reset per PV).
//
// The same PA ordering is used by every domain — so the paIdx -> Particle
// mapping in tracking::Output (`pa_to_particle`) is shared across
// Tracking, Calorimeter, and ParticleId.
template <class F>
void forEachPA(F&& fn) {
  const int LDTOP = phdst::LDTOP;
  if (LDTOP <= 0) return;
  int paIdx = 0;
  for (int lpv = phdst::LQ(LDTOP - 1); lpv > 0; lpv = phdst::LQ(lpv)) {
    for (int lpa = phdst::LQ(lpv - 1); lpa > 0;
         lpa = phdst::LQ(lpa), ++paIdx) {
      fn(lpa, paIdx);
    }
  }
}

}  // namespace delphi_edm4hep::pawalk
