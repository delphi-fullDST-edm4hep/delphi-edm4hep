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

// ---- Commons behind the secondary-vertex search and the combined tag ------
//
// AABTGS -> AASIGN -> AAFSEC runs DELPHI's secondary-vertex search on every
// event (per jet, pair seeds with chi2 < 4, grown with Delta chi2 < 5, L/sigma
// > 4, L < 2.5 cm) and keeps the result in AASCND; AASIGN then uses it to
// re-sign impact parameters. The combined tag of DELPHI 97-094 (AACMBT ->
// AACMSV / AACMNS, per jet; AACMZ0 for the event) reads AASCND, AAJESV and
// AAJETS and writes AACTVR / AACTRS. None of this reaches a DST bank; it is
// read here straight from the commons after PSFBTG.
//
// Sizes below are `nm -S` on libdstanaxx.a (AABTAG v7.14/02). libkalxx.a
// carries an older AASCND (0x3b64, no IF3DSV); the linker keeps the largest
// definition, which is the v7.14 one the accessors assume.

inline constexpr int kMaxJets     = 10;   // NJET cap in AAJETS / NSVJMX
inline constexpr int kMaxHypo     = 20;   // secondary-vertex hypotheses
inline constexpr int kMaxSvTracks = 50;   // tracks per hypothesis
inline constexpr int kNTagVars    = 6;    // NTVMX, discriminating variables
inline constexpr int kMaxRapTracks= 20;   // INSVMX, rapidity tracks per jet

extern "C" {

// COMMON /AAJETS/ — AABTAG's own jets (LUCLUS in JADE mode, ymin = 0.01, run
// inside AATHRS) and the per-track jet/hemisphere assignment.
//   PTHR   thrust direction (4-vector as LUND returns it)
//   NJET   number of jets (<= 10)
//   PJET   jet 4-momenta (px, py, pz, E)
//   IJET   jet number of each track; ITHR its thrust hemisphere (1/2)
//   PHIV   impact-parameter sign of each track (+1 / -1)
//   THRVAL / OBLVAL  thrust and oblateness
//   DISTJ / ERRTJ    3-D track-jet distance and its error (cm)
//   RPDT   track rapidity with respect to its jet
struct AajetsCommon {
  float        pthr[4];
  std::int32_t njet;
  float        pjet[kMaxJets][4];
  float        phijet[kMaxJets];
  std::int32_t ijet [kMaxTracks];
  float        phiv [kMaxTracks];
  std::int32_t ilund[kMaxTracks];
  float        thrval;
  float        oblval;
  std::int32_t ithr [kMaxTracks];
  float        distj[kMaxTracks];
  float        errtj[kMaxTracks];
  float        rpdt [kMaxTracks];
};
extern AajetsCommon aajets_;

// COMMON /AASCND/ — the secondary-vertex hypotheses AAFSEC found.
//   NHYPO  number of hypotheses (<= 20), accepted or rejected
//   ITSEC  >= 0 accepted; < 0 rejected, the value says why (-2 no
//          significant track, -4 track probability, -5 chi2sv, -8 pt, ...)
//   VSEC / SSEC   position (cm) and covariance (cm^2, xx xy yy xz yz zz)
//   NSEC / ISEC   tracks in the hypothesis, as signed AABTAG track indices
//                 (negative: track entered the fit in R-phi only)
//   PSEC   4-momentum of those tracks; PRAP the same for the tracks added by
//          the rapidity criterion (NRAP / IRAP / PTRRAP)
//   TPHSEC / VTPHS  polar/azimuthal angle of the PV->SV direction and its
//          covariance
//   PRBSEC track probability of the vertex tracks; NSVRT tracks that fitted
//   CHI2SV chi2 of the SV impact parameter with respect to the PV (2 d.o.f.
//          in 3-D; the R-phi-only value is scaled by 2 to be comparable)
//   DCASEC (d_rphi, d_z, var_rphi, cov_rphi_z, var_z) of the SV w.r.t. the PV
//   INSV   per track: index of the hypothesis that uses it (+100 flags)
//   JETSV  jet of the hypothesis; IF3DSV 1 if the fit was 3-D
struct AascndCommon {
  std::int32_t nhypo;
  std::int32_t itsec [kMaxHypo];
  float        vsec  [kMaxHypo][3];
  float        ssec  [kMaxHypo][6];
  std::int32_t nsec  [kMaxHypo];
  std::int32_t isec  [kMaxHypo][kMaxSvTracks];
  float        psec  [kMaxHypo][4];
  float        csec  [kMaxHypo];
  float        tphsec[kMaxHypo][2];
  float        vtphs [kMaxHypo][3];
  std::int32_t nrap  [kMaxHypo];
  std::int32_t irap  [kMaxHypo][kMaxSvTracks];
  float        prap  [kMaxHypo][4];
  float        prbsec[kMaxHypo];
  std::int32_t nsvrt [kMaxHypo];
  float        chi2sv[kMaxHypo];
  float        dcasec[kMaxHypo][5];
  float        ptrrap[kMaxHypo][kMaxSvTracks];
  std::int32_t insv  [kMaxTracks];
  std::int32_t jetsv [kMaxHypo];
  std::int32_t if3dsv[kMaxHypo];
};
extern AascndCommon aascnd_;

// COMMON /AAJESV/ — jets redefined by the secondary vertices (AASIGN).
//   NJSEC / IJSEC  jets with an SV, encoded jet + 100 * hypothesis
//   IJSV   per-track jet number after the redefinition
//   PJSV   per-jet direction after the redefinition (PV->SV where an SV
//          exists), indexed by jet number
struct AajesvCommon {
  std::int32_t njsec;
  std::int32_t ijsec[kMaxJets];
  std::int32_t ijsv [kMaxTracks];
  float        pjsv [kMaxTracks][4];
};
extern AajesvCommon aajesv_;

// COMMON /AACTVR/ — combined-tag discriminating variables per jet
// (DELPHI 97-094). Indexed by AABTAG jet number.
//   TAGV(1..6,j)  1: -log10 P_jet+, 2: SV mass (GeV), 3: charged energy
//                 fraction, 4: log10 p_t of the SV (GeV), 5: lepton p_t
//                 (-1 none), 6: unused
//   RATVQ / RATVC  per-variable probability ratios uds/b and c/b
//   NTRS / INVTS / TGVT  tracks entering the rapidity variable, their
//                 AABTAG index and rapidity; RTTVQI / RTTVCI their ratios;
//                 RTTVQ / RTTVC the products
//   RATCQ / RATCC  the jet's combined ratios (input to AACMZ0)
struct AactvrCommon {
  float        xnq1, xnq2, xnq3, xnc1, xnc2, xnc3, xnb1, xnb2, xnb3;
  float        tagv  [kMaxJets][kNTagVars];
  float        ratvq [kMaxJets][kNTagVars];
  float        ratvc [kMaxJets][kNTagVars];
  std::int32_t ntrs  [kMaxJets][1];
  std::int32_t invts [kMaxJets][1][kMaxRapTracks];
  float        tgvt  [kMaxJets][1][kMaxRapTracks];
  float        rttvqi[kMaxJets][1][kMaxRapTracks];
  float        rttvci[kMaxJets][1][kMaxRapTracks];
  float        rttvq [kMaxJets][1];
  float        rttvc [kMaxJets][1];
  float        ratcq [kMaxJets];
  float        ratcc [kMaxJets];
};
extern AactvrCommon aactvr_;

// COMMON /AACTRS/ — combined-tag results.
//   JTAG(j)  jet category: 1 has an SV, 2 no SV but >= 2 significant
//            tracks, 3 lifetime only, 0 not tagged
//   XEFFJ(j) X_jet = -log10 y (prefilled -5 when not computed)
//   XEFFEV   X_ev from AACMZ0 (sum of the two largest X_jet)
struct AactrsCommon {
  std::int32_t jtag [kMaxJets];
  float        xeffj[kMaxJets];
  float        xeffev;
};
extern AactrsCommon aactrs_;

// COMMON /AACTFL/ — which discriminating variables the combined tag uses.
// AADATA sets all seven to 1.
struct AactflCommon {
  std::int32_t ifltim, ifmass, ifenfr, ifptsv, iflptn, ifvar6, ifrpdt;
};
extern AactflCommon aactfl_;

// Combined-tag entry points. No arguments; they read and write the commons
// above and must run after AABTGS on a valid event.
void aacmbt_();   // per-jet tag -> AACTRS / AACTVR
void aacmz0_();   // event tag (Z0 topology) -> XEFFEV

// CERNLIB RNDM seed access (kernlib). On simulation AABTAG smears the impact
// parameters with RNDM (AAPS9x / AAP9xZ, the "IP fixing") and AALINT, called
// by the combined tag, draws from the same stream to emulate lepton-ID
// inefficiency. Bracketing the combined tag with these keeps the smearing of
// the following event, and therefore the lifetime tag, bit-identical to a
// converter that never called it.
void rdmout_(std::uint32_t* seed);
void rdmin_ (std::uint32_t* seed);

}  // extern "C"

static_assert(sizeof(AajetsCommon) == 3028,
              "AAJETS layout mismatch vs the DELPHI release (expected 0xbd4)");
static_assert(sizeof(AascndCommon) == 15284,
              "AASCND layout mismatch vs the DELPHI release (expected 0x3bb4)");
static_assert(sizeof(AajesvCommon) == 2044,
              "AAJESV layout mismatch vs the DELPHI release (expected 0x7fc)");
static_assert(sizeof(AactvrCommon) == 4156,
              "AACTVR layout mismatch vs the DELPHI release (expected 0x103c)");
static_assert(sizeof(AactrsCommon) == 84,
              "AACTRS layout mismatch vs the DELPHI release (expected 0x54)");
static_assert(sizeof(AactflCommon) == 28,
              "AACTFL layout mismatch vs the DELPHI release (expected 0x1c)");

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

inline std::int32_t& IST   (int i)      { return aamain_.ist   [i - 1]; }

// AAJETS
inline float&        PTHR  (int i)      { return aajets_.pthr  [i - 1]; }
inline std::int32_t& NJET  ()           { return aajets_.njet; }
inline float&        PJET  (int i, int j){ return aajets_.pjet [j - 1][i - 1]; }
inline std::int32_t& IJET  (int i)      { return aajets_.ijet  [i - 1]; }
inline float&        PHIV  (int i)      { return aajets_.phiv  [i - 1]; }
inline float&        THRVAL()           { return aajets_.thrval; }
inline float&        OBLVAL()           { return aajets_.oblval; }
inline std::int32_t& ITHR  (int i)      { return aajets_.ithr  [i - 1]; }
inline float&        DISTJ (int i)      { return aajets_.distj [i - 1]; }
inline float&        ERRTJ (int i)      { return aajets_.errtj [i - 1]; }
inline float&        RPDT  (int i)      { return aajets_.rpdt  [i - 1]; }

// AASCND
inline std::int32_t& NHYPO ()           { return aascnd_.nhypo; }
inline std::int32_t& ITSEC (int h)      { return aascnd_.itsec [h - 1]; }
inline float&        VSEC  (int i, int h){ return aascnd_.vsec  [h - 1][i - 1]; }
inline float&        SSEC  (int i, int h){ return aascnd_.ssec  [h - 1][i - 1]; }
inline std::int32_t& NSEC  (int h)      { return aascnd_.nsec  [h - 1]; }
inline std::int32_t& ISEC  (int k, int h){ return aascnd_.isec  [h - 1][k - 1]; }
inline float&        PSEC  (int i, int h){ return aascnd_.psec  [h - 1][i - 1]; }
inline float&        CSEC  (int h)      { return aascnd_.csec  [h - 1]; }
inline float&        TPHSEC(int i, int h){ return aascnd_.tphsec[h - 1][i - 1]; }
inline float&        VTPHS (int i, int h){ return aascnd_.vtphs [h - 1][i - 1]; }
inline std::int32_t& NRAP  (int h)      { return aascnd_.nrap  [h - 1]; }
inline std::int32_t& IRAP  (int k, int h){ return aascnd_.irap  [h - 1][k - 1]; }
inline float&        PRAP  (int i, int h){ return aascnd_.prap  [h - 1][i - 1]; }
inline float&        PRBSEC(int h)      { return aascnd_.prbsec[h - 1]; }
inline std::int32_t& NSVRT (int h)      { return aascnd_.nsvrt [h - 1]; }
inline float&        CHI2SV(int h)      { return aascnd_.chi2sv[h - 1]; }
inline float&        DCASEC(int i, int h){ return aascnd_.dcasec[h - 1][i - 1]; }
inline std::int32_t& INSV  (int i)      { return aascnd_.insv  [i - 1]; }
inline std::int32_t& JETSV (int h)      { return aascnd_.jetsv [h - 1]; }
inline std::int32_t& IF3DSV(int h)      { return aascnd_.if3dsv[h - 1]; }

// AAJESV
inline std::int32_t& NJSEC ()           { return aajesv_.njsec; }
inline std::int32_t& IJSEC (int j)      { return aajesv_.ijsec [j - 1]; }
inline std::int32_t& IJSV  (int i)      { return aajesv_.ijsv  [i - 1]; }
inline float&        PJSV  (int i, int j){ return aajesv_.pjsv  [j - 1][i - 1]; }

// AACTVR / AACTRS
inline float&        TAGV  (int i, int j){ return aactvr_.tagv  [j - 1][i - 1]; }
inline float&        RATVQ (int i, int j){ return aactvr_.ratvq [j - 1][i - 1]; }
inline float&        RATVC (int i, int j){ return aactvr_.ratvc [j - 1][i - 1]; }
inline std::int32_t& NTRS  (int j)      { return aactvr_.ntrs  [j - 1][0]; }
inline std::int32_t& INVTS (int k, int j){ return aactvr_.invts [j - 1][0][k - 1]; }
inline float&        TGVT  (int k, int j){ return aactvr_.tgvt  [j - 1][0][k - 1]; }
inline float&        RTTVQI(int k, int j){ return aactvr_.rttvqi[j - 1][0][k - 1]; }
inline float&        RTTVCI(int k, int j){ return aactvr_.rttvci[j - 1][0][k - 1]; }
inline float&        RTTVQ (int j)      { return aactvr_.rttvq [j - 1][0]; }
inline float&        RTTVC (int j)      { return aactvr_.rttvc [j - 1][0]; }
inline float&        RATCQ (int j)      { return aactvr_.ratcq [j - 1]; }
inline float&        RATCC (int j)      { return aactvr_.ratcc [j - 1]; }
inline std::int32_t& JTAG  (int j)      { return aactrs_.jtag  [j - 1]; }
inline float&        XEFFJ (int j)      { return aactrs_.xeffj [j - 1]; }
inline float&        XEFFEV()           { return aactrs_.xeffev; }

}  // namespace delphi_edm4hep::aabtag
