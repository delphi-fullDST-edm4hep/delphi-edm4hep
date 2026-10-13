// AabtagSvFit.h -- the chi2 of AABTAG's secondary-vertex fits.
//
// AAFSEC fits every secondary-vertex hypothesis with ccFVT3 (3-D) or its entry
// ccFV23 (R-phi), and copies the fitted position into VSEC. A good hypothesis
// whose track list changed then gets a final fit, ccFVT1 followed by ccFVT2
// (outlier removal), written straight into VSEC. Each time AAFSEC drops the
// fit's chi2 and ndf: the line that would keep them, `chi2sv(nhypo) =
// chi2vs`, is commented out in AABTAG 7.14/02, and CHI2SV is later filled
// with the vertex's displacement significance instead.
//
// delphi_sdst_pass and delphi_fdst_pass link with -Wl,--wrap for ccfvt2_,
// ccfvt3_ and ccfv23_, so every call to them passes through AabtagSvFit.cpp,
// which records the result for the current PHDST event. A hypothesis is
// matched back to its last fit by position, which the fit writes and nothing
// changes afterwards. AABTAG's primary-vertex fit (AAFITV) also calls ccFVT2;
// those fits are recorded too but never match a secondary vertex.

#pragma once

#include <optional>

namespace delphi_edm4hep::aabtag {

struct SvFit {
  float chi2;
  int ndf;
};

// The successful fit in the current event whose vertex is exactly `position`
// (cm, as VSEC holds it), or nothing if no fit produced that position.
std::optional<SvFit> svFitAt(const float position[3]);

}  // namespace delphi_edm4hep::aabtag
