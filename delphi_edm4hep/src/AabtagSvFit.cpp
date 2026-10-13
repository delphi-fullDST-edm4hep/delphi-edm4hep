// AabtagSvFit — records AABTAG's secondary-vertex fits; see AabtagSvFit.h.

#include "delphi_edm4hep/internal/AabtagSvFit.h"

#include "phdst/phciii.hpp"

#include <cstring>
#include <vector>

namespace {

struct Record {
  float position[3];
  delphi_edm4hep::aabtag::SvFit fit;
};

// Fits made for the current PHDST event. A data fullDST delivers an event as
// several records, and each record's fits are identical, so they may pile up
// until the run or event number changes.
std::vector<Record> g_fits;
int g_run = 0;
int g_evt = 0;

void record(const float* position, const float* chi2, const int* ndf,
            const int* ierr) {
  if (phdst::IIIRUN != g_run || phdst::IIIEVT != g_evt) {
    g_fits.clear();
    g_run = phdst::IIIRUN;
    g_evt = phdst::IIIEVT;
  }
  if (*ierr != 0) return;  // AAFSEC abandons the fit on an error
  g_fits.push_back({{position[0], position[1], position[2]}, {*chi2, *ndf}});
}

}  // namespace

// ccFVT2, ccFVT3 and its entry ccFV23 share one argument list, every argument
// by reference:
//   (NTRK, FITWB, INVTX, TRACK, wgt, BEAM, WGTBEA, POSVX, COVVX, CHI2, NDF,
//    CHI2TR, VOLD, IERR, A, B, C, phi0, ifz)
// Only POSVX, CHI2, NDF and IERR are read here; the rest pass through
// untouched. The wrapper records after the real fit returns, so a fit nested
// inside another is recorded first and the outer one, which is what the
// caller keeps, last. The __wrap_ / __real_ names are the ones `ld --wrap`
// binds, so they cannot follow the usual C++ naming rules.
#define DELPHI_WRAP_VERTEX_FIT(name)                                          \
  void __real_##name(void*, void*, void*, void*, void*, void*, void*, float*, \
                     void*, float*, int*, void*, void*, int*, void*, void*,   \
                     void*, void*, void*);                                    \
  void __wrap_##name(void* ntrk, void* fitwb, void* invtx, void* track,       \
                     void* wgt, void* beam, void* wgtbea, float* posvx,       \
                     void* covvx, float* chi2, int* ndf, void* chi2tr,        \
                     void* vold, int* ierr, void* a, void* b, void* c,        \
                     void* phi0, void* ifz) {                                 \
    __real_##name(ntrk, fitwb, invtx, track, wgt, beam, wgtbea, posvx, covvx, \
                  chi2, ndf, chi2tr, vold, ierr, a, b, c, phi0, ifz);         \
    record(posvx, chi2, ndf, ierr);                                           \
  }

extern "C" {
DELPHI_WRAP_VERTEX_FIT(ccfvt2_)
DELPHI_WRAP_VERTEX_FIT(ccfvt3_)
DELPHI_WRAP_VERTEX_FIT(ccfv23_)
}

#undef DELPHI_WRAP_VERTEX_FIT

namespace delphi_edm4hep::aabtag {

std::optional<SvFit> svFitAt(const float position[3]) {
  if (phdst::IIIRUN != g_run || phdst::IIIEVT != g_evt) return std::nullopt;
  for (auto it = g_fits.rbegin(); it != g_fits.rend(); ++it) {
    if (std::memcmp(it->position, position, sizeof it->position) == 0)
      return it->fit;
  }
  return std::nullopt;
}

}  // namespace delphi_edm4hep::aabtag
