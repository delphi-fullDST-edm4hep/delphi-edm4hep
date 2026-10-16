// BsaurusCommons.h — internal header (not exported).
//
// C++ views onto the BSAURUS Fortran COMMON blocks and its entry point.
// BSAURUS is DELPHI's inclusive B-reconstruction package (DELPHI 2000-069
// PHYS 868, bsaurus.car in the dstana release): one CALL BSAURUS per event,
// after PSBEG, fills /CSAURUS/ with event, hemisphere and per-particle words.
// The package is self-driving -- FORRUN runs ELEPHANT, its own AABTAG pass
// (PXBTAG('RUN')), MAMMOTH, the RICH decoding and PXLEID lepton ID -- so the
// caller only has to protect the state those leave behind (see Bsaurus.cpp).
//
// LAYOUT IS LOAD-BEARING, as in AabtagCommons.h: Fortran COMMON has no padding,
// members follow declaration order, and the static_asserts at the bottom pin
// each struct against `nm -S libbsaurusxx.a` (dstana release 161018).
//
// Fortran is column-major: BSHEM(NVHEM,2) is bshem[ihem-1][k-1], and the
// accessors take 1-based Fortran indices like the skelana/*.hpp wrappers.

#pragma once

#include <cstdint>

namespace delphi_edm4hep::bsaurus {

inline constexpr int   kMaxParticles = 400;   // NPMAX  (CFORVEC)
inline constexpr int   kNEventWords  = 20;    // NVEVT  (CSAURUS)
inline constexpr int   kNHemWords    = 165;   // NVHEM
inline constexpr int   kNTrackWords  = 47;    // NVPAR
// BSAURUS VFILLs BSEVT / BSHEM / BSPAR with this before processing an event
// and only overwrites what it computes, so an exact -999 means "not filled".
inline constexpr float kSentinel     = -999.f;

extern "C" {

// COMMON /CSAURUS/ — the package output.
//   ISAURUS(2)  per-hemisphere status: 0 not processed, 1 event accepted,
//               2 rapidity algorithm ran, 3 rapidity energy > 10 GeV,
//               4 secondary-vertex fit done, 5 fit converged
//   BSEVT       event words, BSHEM hemisphere words, BSPAR per-particle
//               words in FORVEC's own particle order (1..NTOT); the slot
//               names are in BsaurusWords.h
struct CsaurusCommon {
  std::int32_t isaurus[2];
  float        bsevt[kNEventWords];
  float        bshem[2][kNHemWords];
  float        bspar[kMaxParticles][kNTrackWords];
};
extern CsaurusCommon csaurus_;

// COMMON /CVERT/ — vertices in cm: PVX/CVX the primary vertex BSAURUS uses
// (AABTAG's POSVX/COVVX, z from ELEPHANT before 1994), FSVTX/FSCVX the fitted
// secondary vertex per hemisphere, FPVTX/FPCVX the per-hemisphere primary
// vertex refit, SIGPHI/SIGTHET the direction errors of the SV fit.
struct CvertCommon {
  float pvx[3];
  float cvx[6];
  float fsvtx[2][3];
  float fscvx[2][6];
  float fpvtx[2][3];
  float fpcvx[2][6];
  float sigphi[2];
  float sigthet[2];
};
extern CvertCommon cvert_;

// COMMON /HEMJETS/ (the Fortran name, which the symbol below must keep) — the
// LUCLUS cluster BSAURUS chose as the B candidate of each hemisphere: HBJET its
// index in BSAURUS's own cluster list (energy ordered, 1 = most energetic),
// EREFH the two-body reference energy, PJTHEM its 4-vector (also the BCLUS
// hemisphere words), NJTHEM the clusters in the hemisphere.
struct HemclusCommon {
  std::int32_t hbjet[2];
  float        erefh[2];
  float        pjthem[2][4];
  std::int32_t njthem[2];
};
extern HemclusCommon hemjets_;

// COMMON /CYBFIT/ — secondary-vertex fit bookkeeping. INSEC(i) = 1 marks a
// particle in the fitted vertex (also BSPAR(IBP_SV)); VCHI2/NDOF are the
// fit chi2 and degrees of freedom per hemisphere where the fit filled them.
struct CybfitCommon {
  std::int32_t ivuse[kMaxParticles];
  std::int32_t isuse[kMaxParticles];
  std::int32_t mapv [kMaxParticles];
  std::int32_t npav, nacf;
  float        start;
  float        flight[2], eflight[2], probfit[2];
  std::int32_t ifitcode[2];
  std::int32_t npall[2], npcut[2], npgc[2], npgood[2];
  float        pnet_cut;
  std::int32_t numb[2];
  float        var_sec1, var_sec2, dippy;
  std::int32_t insec[kMaxParticles];
  float        vchi2[2];
  std::int32_t ndof[2];
};
extern CybfitCommon cybfit_;

// COMMON /CFORVEC/ — FORVEC's particle list: NPL/NMI/NEU positive, negative
// and neutral counts, NCH = NPL + NMI charged first, NTOT = NCH + NEU. The
// particle arrays are sorted by charge class then descending momentum, so
// row i of BSPAR is row i here.
struct CforvecCommon {
  std::int32_t l_pmax;                 // Fortran LOGICAL*4: list truncated
  std::int32_t npl, nmi, neu, nch, ntot;
  float        p    [kMaxParticles][6];
  float        pinfo[kMaxParticles][15];
  float        uvar [kMaxParticles][20];
  std::int32_t iuvar[kMaxParticles][20];
  std::int32_t info [kMaxParticles][11];
};
extern CforvecCommon cforvec_;

// COMMON /LFORSAV/ — ZEBRA L-address of each particle's PA bank, parallel to
// CFORVEC. The same key AABTAG's IADTR uses, so it maps onto the PA walk.
struct LforsavCommon {
  std::int32_t lpavec[kMaxParticles];
};
extern LforsavCommon lforsav_;

// The package entry point: SKELANA interface, no arguments, once per event.
void bsaurus_();

}  // extern "C"

static_assert(sizeof(CsaurusCommon) == 76608,
              "CSAURUS layout mismatch vs the DELPHI release (expected 0x12b40)");
static_assert(sizeof(CvertCommon) == 196,
              "CVERT layout mismatch vs the DELPHI release (expected 0xc4)");
static_assert(sizeof(HemclusCommon) == 56,
              "HEMJETS layout mismatch vs the DELPHI release (expected 0x38)");
static_assert(sizeof(CybfitCommon) == 6516,
              "CYBFIT layout mismatch vs the DELPHI release (expected 0x1974)");
static_assert(sizeof(CforvecCommon) == 115224,
              "CFORVEC layout mismatch vs the DELPHI release (expected 0x1c218)");
static_assert(sizeof(LforsavCommon) == 1600,
              "LFORSAV layout mismatch vs the DELPHI release (expected 0x640)");

// ---- 1-based accessors ------------------------------------------------------

inline std::int32_t& ISAURUS(int ih)        { return csaurus_.isaurus[ih - 1]; }
inline float&        BSEVT(int k)           { return csaurus_.bsevt[k - 1]; }
inline float&        BSHEM(int k, int ih)   { return csaurus_.bshem[ih - 1][k - 1]; }
inline float&        BSPAR(int k, int i)    { return csaurus_.bspar[i - 1][k - 1]; }

inline std::int32_t& NTOT()                 { return cforvec_.ntot; }
inline std::int32_t& NCH()                  { return cforvec_.nch; }
inline std::int32_t& LPAVEC(int i)          { return lforsav_.lpavec[i - 1]; }

inline float&        VCHI2(int ih)          { return cybfit_.vchi2[ih - 1]; }
inline std::int32_t& NDOF(int ih)           { return cybfit_.ndof[ih - 1]; }

inline std::int32_t& HBCLUS(int ih)         { return hemjets_.hbjet[ih - 1]; }
inline std::int32_t& NCLUSHEM(int ih)       { return hemjets_.njthem[ih - 1]; }

}  // namespace delphi_edm4hep::bsaurus
