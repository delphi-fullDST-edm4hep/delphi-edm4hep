// AabtagCommons.h — internal header (not exported).
//
// C++ views onto the AABTAG Fortran COMMON blocks. These are NOT part of
// delphi-analysis/include/skelana/* (which only wraps the SKELANA PSC*
// commons), so they are declared here.
//
// AABTAG is DELPHI's b-tagging package. SKELANA drives it from PSFBTG when
// IFLBTG > 0 (see skelana.car): on a fullDST *any* IFLBTG > 0 recalculates;
// on a shortDST only IFLBTG == 2 does, while IFLBTG == 1 reads the stored
// BTAG bank via PSHBTG and leaves the commons below untouched. Read them
// only after a PSFBTG-producing configuration.
//
// LAYOUT IS LOAD-BEARING. Fortran COMMON has no padding and members are
// laid out in declaration order; the static_asserts at the bottom pin the
// total size of each struct against the size the shipped archives report
// (`nm` on libbsaurusxx.a: aamain_ = 0xb548 = 46408, aamnvx_ = 0xcb4 =
// 3252). If a future DELPHI release changes a dimension, those asserts
// fire at compile time instead of silently reading garbage.
//
// Fortran is column-major: PAR(5,100) is indexed par[j-1][i-1] from
// PAR(i,j). The accessors below take 1-based Fortran indices to match the
// skelana/*.hpp idiom used everywhere else in this library.

#pragma once

#include <cstddef>
#include <cstdint>

namespace delphi_edm4hep::aabtag {

// Hard cap baked into the AABTAG commons. NTRK saturates at this value, so
// reaching it means additional eligible tracks may have been truncated.
inline constexpr int kMaxTracks = 100;

extern "C" {

// COMMON /AAMAIN/ — per-track track/VD/probability arrays.
//   NTRK   number of tracks AABTAG considered
//   XGOOD  number of "good" tracks
//   IADTR  ZEBRA L-address of each track's PA bank (maps back to our PA walk)
//   ISRT   0 = track NOT used for b-tagging, > 0 = used
//   TRPR   per-track probability, r-phi     <- the jet-probability ingredient
//   TRPRZ  per-track probability, z
//   NVDP / NVDPZ    VD hits, r-phi / z
//   NLAY / NLAYZ    VD layers with hits, r-phi / z
//   CHI2VD chi^2 of the VD hits associated to the track
//   CHI2TO track chi^2
struct AamainCommon {
  std::int32_t ntrk;
  float        xgood;
  std::int32_t iadtr [kMaxTracks];
  std::int32_t ist   [kMaxTracks];
  std::int32_t nvdp  [kMaxTracks];
  float        chi2vd[kMaxTracks];
  std::int32_t isrt  [kMaxTracks];
  float        par   [kMaxTracks][5];
  float        wgt   [kMaxTracks][15];
  double       track [kMaxTracks][5];
  float        trpr  [kMaxTracks];
  std::int32_t laypat[kMaxTracks];
  std::int32_t nlay  [kMaxTracks];
  float        pmom  [kMaxTracks];
  float        chi2to[kMaxTracks];
  std::int32_t ivdhit[kMaxTracks][6];
  std::int32_t nvdpz [kMaxTracks];
  std::int32_t laypz [kMaxTracks];
  std::int32_t nlayz [kMaxTracks];
  std::int32_t ivdhz [kMaxTracks][6];
  double       errtk [kMaxTracks][15];
  double       errp  [kMaxTracks][15];
  float        trprz [kMaxTracks];
};
extern AamainCommon aamain_;

// COMMON /AAMNVX/ — AABTAG's primary vertex and the per-track impact
// parameters measured with respect to it.
//   POSVX / COVVX   fitted PV position (cm) and covariance (cm^2), the
//                   values SKELANA copies into QVTX(...,1) when IFLPVT==1
//   NATTVX/NDOFVX   tracks attached to the vertex / d.o.f. of the fit
//   PARIMP / SIGIMP signed r-phi impact parameter wrt POSVX and its error
//   EZED   / SIGZED z impact parameter at closest approach and its error
//   CHI2TR          per-track contribution to the vertex chi^2
//   INMVX           Fortran LOGICAL(4): track attached to the main vertex
struct AamnvxCommon {
  std::int32_t ntrvx;
  float        posvx[3];
  float        covvx[6];
  float        chi2vx;
  std::int32_t nattvx;
  std::int32_t ndofvx;
  float        chi2tr[kMaxTracks];
  float        parimp[kMaxTracks];
  float        ezed  [kMaxTracks];
  float        sigimp[kMaxTracks];
  float        sigzed[kMaxTracks];
  std::int32_t inmvx [kMaxTracks];   // Fortran LOGICAL*4
  float        xkvrt [kMaxTracks];
  float        zkvrt [kMaxTracks];
};
extern AamnvxCommon aamnvx_;

// COMMON /AAFLAG/ — AABTAG steering and per-event status. Most members are
// Fortran LOGICAL*4; IBAD (the fifth slot) and IFBFLA are INTEGER*4.
// IBAD=0 means the tag/PV processing succeeded; 1 is a general processing
// failure (for example beamspot/thrust/calibration), and 2 is a vertex-fit
// failure. On failure some AAMNVX arrays can retain stale pre-fit values.
// Moreover, PSFBTG does not call AABTGS at all when IERRBS != 0, so IBAD then
// remains a snapshot from an earlier event. Never use it as a current-event
// validity bit without the separate invocation state (AabtagStatus.h).
struct AaflagCommon {
  std::int32_t ifclbr;
  std::int32_t iftcor;
  std::int32_t if91;
  std::int32_t ifk0ls;
  std::int32_t ibad;
  std::int32_t ifold;
  std::int32_t ifrfix;
  std::int32_t ifk0st;
  std::int32_t ifjets;
  std::int32_t ifstcm;
  std::int32_t ifbcon;
  std::int32_t ifbfla;
  std::int32_t ifbcvx;
  std::int32_t ifspot;
};
extern AaflagCommon aaflag_;

}  // extern "C"

// Sizes reported by the shipped archives. A mismatch means the release's
// common layout no longer matches this header — fail the build, loudly.
static_assert(sizeof(AamainCommon) == 46408,
              "AAMAIN layout mismatch vs the DELPHI release (expected 0xb548)");
static_assert(sizeof(AamnvxCommon) == 3252,
              "AAMNVX layout mismatch vs the DELPHI release (expected 0xcb4)");
static_assert(sizeof(AaflagCommon) == 56,
              "AAFLAG layout mismatch vs the DELPHI release (expected 0x38)");

// ---- 1-based accessors, matching the skelana/*.hpp convention ----------

inline std::int32_t& NTRK()             { return aamain_.ntrk; }
inline float&        XGOOD()            { return aamain_.xgood; }
inline std::int32_t& IADTR (int i)      { return aamain_.iadtr [i - 1]; }
inline std::int32_t& ISRT  (int i)      { return aamain_.isrt  [i - 1]; }
inline float&        TRPR  (int i)      { return aamain_.trpr  [i - 1]; }
inline float&        TRPRZ (int i)      { return aamain_.trprz [i - 1]; }
inline std::int32_t& NVDP  (int i)      { return aamain_.nvdp  [i - 1]; }
inline std::int32_t& NVDPZ (int i)      { return aamain_.nvdpz [i - 1]; }
inline std::int32_t& NLAY  (int i)      { return aamain_.nlay  [i - 1]; }
inline std::int32_t& NLAYZ (int i)      { return aamain_.nlayz [i - 1]; }
inline float&        CHI2VD(int i)      { return aamain_.chi2vd[i - 1]; }
inline float&        CHI2TO(int i)      { return aamain_.chi2to[i - 1]; }
inline float&        PMOM  (int i)      { return aamain_.pmom  [i - 1]; }

inline std::int32_t& NTRVX()            { return aamnvx_.ntrvx; }
inline float&        POSVX (int i)      { return aamnvx_.posvx[i - 1]; }
inline float&        COVVX (int i)      { return aamnvx_.covvx[i - 1]; }
inline float&        CHI2VX()           { return aamnvx_.chi2vx; }
inline std::int32_t& NATTVX()           { return aamnvx_.nattvx; }
inline std::int32_t& NDOFVX()           { return aamnvx_.ndofvx; }
inline float&        CHI2TR(int i)      { return aamnvx_.chi2tr[i - 1]; }
inline float&        PARIMP(int i)      { return aamnvx_.parimp[i - 1]; }
inline float&        SIGIMP(int i)      { return aamnvx_.sigimp[i - 1]; }
inline float&        EZED  (int i)      { return aamnvx_.ezed  [i - 1]; }
inline float&        SIGZED(int i)      { return aamnvx_.sigzed[i - 1]; }
inline std::int32_t& INMVX (int i)      { return aamnvx_.inmvx [i - 1]; }
inline std::int32_t& IBAD()              { return aaflag_.ibad; }

}  // namespace delphi_edm4hep::aabtag
