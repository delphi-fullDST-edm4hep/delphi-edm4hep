// Btag.cpp — b-tagging domain implementation.
//
// Reads PSCBTG (event/hemisphere probabilities) and, when AABTAG was
// actually rerun, the AAMAIN / AAMNVX commons (per-track impact
// parameters, per-track probabilities, VD quality, and AABTAG's own
// primary vertex), the AAJETS / AASCND / AAJESV commons (AABTAG's jets and
// the secondary vertices its AAFSEC search found), and -- after calling the
// combined tag of DELPHI 97-094 (AACMBT, AACMZ0) -- AACTVR / AACTRS.

#include "delphi_edm4hep/Btag/Btag.h"

#include "delphi_edm4hep/internal/AabtagCommons.h"
#include "delphi_edm4hep/internal/AabtagStatus.h"
#include "delphi_edm4hep/internal/PaWalk.h"

#include "skelana/functions.hpp"
#include "skelana/pscbsp.hpp"
#include "skelana/pscbtg.hpp"
#include "skelana/pscflg.hpp"

#include <edm4hep/MutableParticleID.h>
#include <edm4hep/ParticleIDCollection.h>
#include <edm4hep/MutableReconstructedParticle.h>
#include <edm4hep/MutableVertex.h>
#include <edm4hep/ReconstructedParticleCollection.h>
#include <edm4hep/VertexCollection.h>
#include <podio/UserDataCollection.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sk = skelana;
namespace aa = delphi_edm4hep::aabtag;

namespace delphi_edm4hep::btag {

namespace {

constexpr double kCm2Mm    = 10.0;
constexpr float  kCm2Mm2_f = static_cast<float>(kCm2Mm * kCm2Mm);
constexpr float  kNaN      = std::numeric_limits<float>::quiet_NaN();

// algorithmType tag for AABTAG's primary vertex. Distinct from the values
// Vertex.cpp uses (0 primary, 1 secondary, 2 beamspot, 4 simulation,
// 10 V0, 11 photon conversion) so the two PVs are never confused.
constexpr int kAlgoBtagPV = 3;

// algorithmType for the per-track b-tag row.
constexpr int kAlgoBtagTag = 4;

// algorithmType for AABTAG's secondary-vertex hypotheses (AAFSEC).
constexpr int kAlgoBtagSV = 5;

// algorithmType for the per-jet combined-tag row (AACMBT).
constexpr int kAlgoBtagCtr = 6;

// Map the "not computed" sentinel to NaN so a consumer that forgets to check
// cannot silently average it in.
// PSFBTG pre-fills every PSCBTG word with 2.0 and only overwrites on success,
// so 2.0 means "not computed". Neither a probability nor a direction cosine
// can legitimately reach it.
float prob(float v) { return (v >= 1.999f) ? kNaN : v; }

// LUTHRU reports failure with THRVAL=-1 or -2; PSCBTG otherwise uses the 2.0
// prefill sentinel. Physical thrust is in [0,1], so map both failure domains.
float thrustValue(float v) { return (v < 0.f || v >= 1.999f) ? kNaN : v; }

}  // namespace

// The PSCBTG common is a single output buffer: PSHBTG fills it from the
// stored BTAG bank and PSFBTG from the recalculation, so whichever ran last
// is what it holds.
void BtagWriter::emitEventLevel(std::string_view bank, Provenance prov,
                               bool valid)
{
  const auto eventProb = [&](float value) { return valid ? prob(value) : kNaN; };
  // Index order within each triplet is (hemisphere 1, hemisphere 2, whole
  // event), matching QBTPRN/QBTPRP/QBTPRS(1..3).
  putParameter(bank, "ProbNegIP",
               std::vector<float>{eventProb(sk::QBTPRN(1)),
                                  eventProb(sk::QBTPRN(2)),
                                  eventProb(sk::QBTPRN(3))}, prov);
  putParameter(bank, "ProbPosIP",
               std::vector<float>{eventProb(sk::QBTPRP(1)),
                                  eventProb(sk::QBTPRP(2)),
                                  eventProb(sk::QBTPRP(3))}, prov);
  putParameter(bank, "ProbAllIP",
               std::vector<float>{eventProb(sk::QBTPRS(1)),
                                  eventProb(sk::QBTPRS(2)),
                                  eventProb(sk::QBTPRS(3))}, prov);
  // The thrust axis gets the same sentinel treatment: VFILL sets it to 2.0
  // as well, and a direction cosine can never legitimately exceed 1, so an
  // un-mapped 2.0 here would be a sentinel masquerading as data.
  putParameter(bank, "ThrustAxis",
               std::vector<float>{eventProb(sk::QBTTHR(1)),
                                  eventProb(sk::QBTTHR(2)),
                                  eventProb(sk::QBTTHR(3))}, prov);
  // QBTTHR(4) is the thrust VALUE, not an axis component.
  putParameter(bank, "ThrustValue",
               valid ? thrustValue(sk::QBTTHR(4)) : kNaN, prov);
}

void BtagWriter::emit()
{
  putParameter("BTAGCFG", "SourcePrefix",
               std::string(fromFullDst() ? "fDST" : "sDST"), Provenance::Custom);
  putParameter("BTAGCFG", "BeamSpotErrorCode", sk::IERRBS,
               Provenance::Derived);

  // AAFLAG is meaningful only when PSFBTG actually called AABTGS. PSFBTG
  // skips that call when IERRBS != 0 and leaves IBAD (and the rich COMMON
  // arrays) stale. Failed AABTAG events can retain derived values too, so gate
  // the entire rich payload on the combined current-event status rather than
  // sanitizing one field at a time.
  const auto status = aa::eventStatus(sk::IERRBS, aa::IBAD());
  const bool tagValid = status.valid;

  // Both tags are emitted. PSHORT has already run PSFBTG, so the recalculated
  // values are the ones currently in PSCBTG; read them before PSHBTG refills
  // the common from the stored bank. The stored tag needs no validity gate --
  // an absent bank leaves the 2.0 prefill, which prob() maps to NaN.
  emitEventLevel("AABTAG", Provenance::Derived, tagValid);
  sk::PSHBTG();
  emitEventLevel("BTG", Provenance::Transcribed, /*valid=*/true);

  // The per-track arrays and AABTAG's vertex come from AAMAIN / AAMNVX, which
  // PSHBTG does not touch, so they still hold the recalculation.
  const std::string_view bank = "AABTAG";
  const Provenance prov = Provenance::Derived;

  // ---- AABTAG primary-vertex output (AAMNVX) -------------------------
  // Emitted as its own collection rather than replacing the DELANA PV.
  // The collection is empty when Valid != 1. A nonempty entry still
  // needs NDF/NTracksAttached checks before it is described as track-fitted;
  // a status-zero result can be a beamspot-only constraint.
  // With IFLPVT = Keep (the default) SKELANA never overwrites QVTX, so a
  // consumer gets both vertices and picks; nothing is destroyed.
  // Built here but put after the per-track loop, so the tracks AABTAG
  // attached to this vertex can be linked while it is still mutable.
  edm4hep::VertexCollection btagPv;
  std::optional<edm4hep::MutableVertex> pv;
  if (tagValid) {
    pv = btagPv.create();
    pv->setPrimary(true);
    pv->setAlgorithmType(kAlgoBtagPV);
    pv->setPosition({static_cast<float>(aa::POSVX(1) * kCm2Mm),
                     static_cast<float>(aa::POSVX(2) * kCm2Mm),
                     static_cast<float>(aa::POSVX(3) * kCm2Mm)});
    pv->setChi2(aa::CHI2VX());
    pv->setNdf(aa::NDOFVX());
    pv->setCovMatrix({aa::COVVX(1) * kCm2Mm2_f, aa::COVVX(2) * kCm2Mm2_f,
                      aa::COVVX(3) * kCm2Mm2_f, aa::COVVX(4) * kCm2Mm2_f,
                      aa::COVVX(5) * kCm2Mm2_f, aa::COVVX(6) * kCm2Mm2_f});
  }

  // ---- Per-track quantities (AAMAIN + AAMNVX) ------------------------
  // AABTAG's arrays are dimensioned kMaxTracks. Its NTRK common saturates at
  // that capacity, so exact truncation is not observable; record capacity
  // saturation conservatively rather than claiming that extra tracks existed.
  // NTRK belongs to the current event only if AABTGS was called. On the
  // beam-spot bypass it is stale by construction, so serialize zero rather
  // than mislabelling a preceding event's count as raw current-event data.
  const int ntrk_raw = status.algorithmInvoked ? aa::NTRK() : 0;
  const int ntrk = tagValid ? std::clamp(ntrk_raw, 0, aa::kMaxTracks) : 0;
  // BadEventCode deliberately preserves the raw AAFLAG/IBAD snapshot. It is
  // current-event status only when AlgorithmInvoked=1; on the PSFBTG beamspot
  // bypass it can be stale. Valid is the authoritative combined gate.
  putParameter(bank, "BadEventCode",     status.badEventCode, Provenance::Derived);
  putParameter(bank, "AlgorithmInvoked", status.algorithmInvoked ? 1 : 0, Provenance::Custom);
  putParameter(bank, "Valid",            status.valid ? 1 : 0, Provenance::Custom);
  putParameter(bank, "NTracksRaw",       std::clamp(ntrk_raw, 0, aa::kMaxTracks), Provenance::Custom);
  putParameter(bank, "NTracks",         ntrk, Provenance::Custom);
  putParameter(bank, "NTracksAttached", tagValid ? aa::NATTVX() : 0, Provenance::Custom);
  // Retain the established field name for campaign compatibility. Its value
  // is deliberately conservative: 1 means the common reached capacity and
  // additional eligible tracks may (but cannot be proven to) have been lost.
  putParameter(bank, "Truncated",
               tagValid && ntrk_raw >= aa::kMaxTracks ? 1 : 0, Provenance::Custom);

  // lpa -> PA-walk index, so IADTR (a ZEBRA L-address) can be resolved to
  // the Particle the Tracking domain emitted for that PA.
  std::unordered_map<int, int> lpa_to_pa;
  pawalk::forEachPA([&](int lpa, int paIdx) { lpa_to_pa.emplace(lpa, paIdx); });

  // One row per track AABTAG used, in its own 1..NTRK ordering, linked to
  // the particle it belongs to. The impact parameters are NOT here: they are
  // a property of the track and ride on it as a TrackState at AABTAG's
  // vertex (see Tracking.cpp). Reach them from a row via
  // getParticle() -> getTracks() -> the AtVertex state.
  edm4hep::ParticleIDCollection tags;

  for (int i = 1; i <= ntrk; ++i) {
    auto tag = tags.create();
    tag.setAlgorithmType(kAlgoBtagTag);

    // AAMNVX defines CHI2TR for attached tracks only. The slot is not cleared
    // for the rest, so it holds whatever the fit last left there -- often the
    // preceding event's value. Publish NaN rather than that.
    const bool attached = aa::INMVX(i);

    // AATPRB leaves the probabilities at 1.0 for tracks it could not use.
    tag.addToParameters(aa::TRPR (i));
    tag.addToParameters(aa::TRPRZ(i));
    tag.addToParameters(aa::CHI2VD(i));
    tag.addToParameters(attached ? aa::CHI2TR(i) : kNaN);
    tag.addToParameters(aa::PMOM  (i));
    // These count-like legacy values are signed: AAP* efficiency/acceptance
    // corrections negate them to mark rejection; abs(value) is the count.
    tag.addToParameters(static_cast<float>(aa::NVDP (i)));
    tag.addToParameters(static_cast<float>(aa::NVDPZ(i)));
    tag.addToParameters(static_cast<float>(aa::NLAY (i)));
    tag.addToParameters(static_cast<float>(aa::NLAYZ(i)));
    tag.addToParameters(static_cast<float>(aa::ISRT(i)));        // 0 = unused
    tag.addToParameters(static_cast<float>(attached ? 1 : 0));
    // [11..19] jet / hemisphere / sign bookkeeping from AAJETS, AASCND, AAJESV
    // and the AAMAIN status word. Appended after the original 11 so existing
    // readers keep their indices.
    tag.addToParameters(static_cast<float>(aa::IJET(i)));        // CombinedTagRow row, 1-based
    tag.addToParameters(static_cast<float>(aa::ITHR(i)));        // thrust hemisphere (1/2)
    // PHIV is not a sign, despite what the AAMAIN header says. AASIGN fills it
    // through AASGNT -> AADCAJ -> AADIST -> POINTF, which solves
    // s1 = PV + al * jet_direction for the two points of closest approach
    // between the track helix and the jet axis and returns `al`: the signed
    // distance ALONG THE JET AXIS, in cm, from the primary vertex to that
    // point. Its sign is what signs the impact parameter -- which is all the
    // DELPHI comment ever meant -- and AADCAJ's own header says so. Left in cm
    // deliberately: the value degrades to a bare +-1 whenever the distance
    // below is a placeholder, so it is not a length everywhere.
    tag.addToParameters(aa::PHIV(i));
    tag.addToParameters(aa::RPDT(i));                            // rapidity w.r.t. its jet
    // AASGNT abandons the track-jet distance in two places and leaves a
    // placeholder rather than a measurement: a track with no VD z-hits or a
    // primary vertex with a bad z-covariance gives dist = 0, err = 100 cm, and
    // AADCAJ bails out on a null jet momentum with dist = 0, err = sqrt(200)
    // cm. Together that is a third of all tracks (34% over 70k tracks of 94c
    // data), and their err = 1000 mm drags the mean of the column from 0.27 mm
    // to 336 mm. Map both to NaN, as prob() does for PSCBTG's 2.0; in those
    // rows only the SIGN of PHIV above carries information.
    const bool jetDistMeasured = aa::DISTJ(i) != 0.f && aa::ERRTJ(i) < 100.f;
    tag.addToParameters(jetDistMeasured ? static_cast<float>(aa::DISTJ(i) * kCm2Mm) : kNaN);
    tag.addToParameters(jetDistMeasured ? static_cast<float>(aa::ERRTJ(i) * kCm2Mm) : kNaN);
    tag.addToParameters(static_cast<float>(aa::INSV(i)));        // SV hypothesis using it (+100 flags)
    // Track-quality code as AASTRK / AASLCT / AAIMPC leave it, i.e. before the
    // combined tag runs: -98 no VD hits (or p > 1.1 Ebeam), -90 passed an
    // NLAY/CHI2VD class, 10 within AAIMPC's impact-parameter significance cut,
    // -99 AASTRK's initial value where nothing reclassified it. AAK0LS can also
    // write 99 (not in the track list, rewritten to -90 by the combined tag)
    // and 200/300/400 (K0 / Lambda / conversion daughter), but neither appeared
    // in 70k tracks of 94c data -- see docs/README.md.
    tag.addToParameters(static_cast<float>(aa::IST(i)));
    tag.addToParameters(static_cast<float>(aa::IJSV(i)));        // ditto, after the SV redefinition

    if (auto it = lpa_to_pa.find(aa::IADTR(i)); it != lpa_to_pa.end()) {
      if (const auto particle = particleForPa(it->second)) {
        tag.setParticle(*particle);
        // Tracks AABTAG attached to its own vertex, as a relation rather
        // than a flag to re-derive.
        if (pv && attached) pv->addToParticles(*particle);
      }
    }
  }

  put(std::move(tags), bank, "TrackTag", prov);
  put(std::move(btagPv), bank, "PrimaryVertex", prov);

  emitCombinedTag(tagValid, lpa_to_pa);
}

// ---------------------------------------------------------------------------
// Secondary vertices and the combined tag.
//
// AABTGS (called by PSFBTG) already ran AAFSEC, DELPHI's secondary-vertex
// search, and AASIGN used it to re-sign the impact parameters; the vertices
// themselves stay in the AASCND common and are never written to a bank. The
// combined tag (DELPHI 97-094) is a separate entry point, AACMBT, that PSFBTG
// never calls; it consumes those commons and fills AACTVR / AACTRS per jet,
// and AACMZ0 turns the per-jet values into the event tag. This is the same
// sequence BSAURUS's PXBTAG('RUN') uses.
//
// Output (all Derived, bank AABTAG):
//   SecondaryVertices  one Vertex per AAFSEC hypothesis, accepted or not
//                      (parameters[0] = ITSEC says which); position mm,
//                      covariance mm^2, chi2 = CHI2SV (2 d.o.f., see the
//                      AASCND comment), particles = the fitted tracks.
//   CombinedTagRow     one ParticleID per AABTAG jet, in jet order: type =
//                      category JTAG, likelihood = X_jet, parameters as
//                      documented below. AABTAG's own jets are deliberately
//                      NOT emitted as ReconstructedParticles -- EDM4hep has
//                      no agreed jet type, and this JADE clustering
//                      (y_min = 0.01) is not one anybody would use today --
//                      but the axis every variable refers to is kept as the
//                      first four parameters of the row, and the tracks are
//                      kept through the per-track IJET word on TrackTag.
//   CombinedTagEvent   X_ev (AACMZ0); CombinedTagHemisphere the largest
//                      X_jet per thrust hemisphere (BSAURUS's BTAG(29,30)).
void BtagWriter::emitCombinedTag(bool valid,
                                 const std::unordered_map<int, int>& lpa_to_pa)
{
  const std::string_view bank = "AABTAG";
  const Provenance prov = Provenance::Derived;

  // AABTAG track index (1..NTRK) -> its particle, or empty.
  const auto particleForTrack =
    [&](int i) -> std::optional<edm4hep::ReconstructedParticle> {
      if (i < 1 || i > aa::kMaxTracks) return std::nullopt;
      const auto it = lpa_to_pa.find(aa::IADTR(i));
      if (it == lpa_to_pa.end()) return std::nullopt;
      return particleForPa(it->second);
    };

  // AACMSV bails out on IBAD != 0 but AACMNS does not, and on the beam-spot
  // bypass every common is stale; so call into AABTAG only on a valid event.
  int njet = 0, nhypo = 0;
  if (valid) {
    // On MC, AALINT (lepton-ID tuning inside AACMBT) consumes RNDM numbers
    // that the next event's impact-parameter smearing would otherwise have
    // used; restore the seed so the lifetime tag stays bit-identical to the
    // unmodified converter. See the note on rdmout_/rdmin_ in AabtagCommons.h.
    std::int32_t seed[2] = {0, 0};   // not filled by the routine we link;
    aa::rdmout_(seed);               // SXRNU keeps the state in its own slot 3
    aa::aacmbt_();
    aa::aacmz0_();
    aa::rdmin_(seed);
    njet  = std::clamp(aa::NJET(),  0, aa::kMaxJets);
    nhypo = std::clamp(aa::NHYPO(), 0, aa::kMaxHypo);
  }

  // ---- secondary-vertex hypotheses (AASCND) ------------------------------
  // parameters: [0] ITSEC  [1] NSEC  [2] NSVRT  [3] NRAP
  //   [4] JETSV, the CombinedTagRow row this hypothesis belongs to (1-based)
  //   [5] IF3DSV  [6] PRBSEC  [7] CHI2SV  [8] CSEC
  //   [9..12] PSEC px py pz E (GeV)   [13..16] PRAP px py pz E (GeV)
  //   [17,18] TPHSEC theta, phi of the PV->SV direction (rad)
  //   [19..21] VTPHS its covariance   [22..26] DCASEC d_rphi d_z (mm),
  //   var_rphi cov var_z (mm^2)
  //   [27 .. 27+NSEC-1]  ISEC, signed AABTAG track indices (see AASCND)
  //   [27+NSEC .. +NRAP-1]  IRAP, AABTAG track indices of the rapidity tracks
  edm4hep::VertexCollection svCol;
  int nAccepted = 0;
  for (int h = 1; h <= nhypo; ++h) {
    auto sv = svCol.create();
    sv.setPrimary(false);
    sv.setAlgorithmType(kAlgoBtagSV);
    sv.setPosition({static_cast<float>(aa::VSEC(1, h) * kCm2Mm),
                    static_cast<float>(aa::VSEC(2, h) * kCm2Mm),
                    static_cast<float>(aa::VSEC(3, h) * kCm2Mm)});
    sv.setCovMatrix({aa::SSEC(1, h) * kCm2Mm2_f, aa::SSEC(2, h) * kCm2Mm2_f,
                     aa::SSEC(3, h) * kCm2Mm2_f, aa::SSEC(4, h) * kCm2Mm2_f,
                     aa::SSEC(5, h) * kCm2Mm2_f, aa::SSEC(6, h) * kCm2Mm2_f});
    sv.setChi2(aa::CHI2SV(h));
    sv.setNdf(2);
    const int itsec = aa::ITSEC(h);
    if (itsec >= 0) ++nAccepted;
    const int nsec = std::clamp(aa::NSEC(h), 0, aa::kMaxSvTracks);
    const int nrap = std::clamp(aa::NRAP(h), 0, aa::kMaxSvTracks);
    sv.addToParameters(static_cast<float>(itsec));
    sv.addToParameters(static_cast<float>(nsec));
    sv.addToParameters(static_cast<float>(aa::NSVRT(h)));
    sv.addToParameters(static_cast<float>(nrap));
    sv.addToParameters(static_cast<float>(aa::JETSV(h)));
    sv.addToParameters(static_cast<float>(aa::IF3DSV(h)));
    sv.addToParameters(aa::PRBSEC(h));
    sv.addToParameters(aa::CHI2SV(h));
    sv.addToParameters(aa::CSEC(h));
    for (int i = 1; i <= 4; ++i) sv.addToParameters(aa::PSEC(i, h));
    for (int i = 1; i <= 4; ++i) sv.addToParameters(aa::PRAP(i, h));
    sv.addToParameters(aa::TPHSEC(1, h));
    sv.addToParameters(aa::TPHSEC(2, h));
    for (int i = 1; i <= 3; ++i) sv.addToParameters(aa::VTPHS(i, h));
    sv.addToParameters(static_cast<float>(aa::DCASEC(1, h) * kCm2Mm));
    sv.addToParameters(static_cast<float>(aa::DCASEC(2, h) * kCm2Mm));
    sv.addToParameters(aa::DCASEC(3, h) * kCm2Mm2_f);
    sv.addToParameters(aa::DCASEC(4, h) * kCm2Mm2_f);
    sv.addToParameters(aa::DCASEC(5, h) * kCm2Mm2_f);
    for (int k = 1; k <= nsec; ++k) {
      const int idx = aa::ISEC(k, h);
      sv.addToParameters(static_cast<float>(idx));
      if (const auto p = particleForTrack(std::abs(idx))) sv.addToParticles(*p);
    }
    for (int k = 1; k <= nrap; ++k)
      sv.addToParameters(static_cast<float>(aa::IRAP(k, h)));
  }

  // ---- the per-jet combined tag (AAJETS, AACTVR / AACTRS) ----------------
  // One row per AABTAG jet, in jet order, so the per-track IJET / IJSV words
  // and the per-hypothesis JETSV word index it directly (1-based).
  // CombinedTagRow parameters:
  //   [0..3] PJET px py pz E, the jet axis every variable below refers to
  //   [4] JTAG  [5] XEFFJ  [6] thrust hemisphere (1/2)
  //   [7..12] TAGV(1..6)  [13..18] RATVQ(1..6)  [19..24] RATVC(1..6)
  //   [25] RATCQ  [26] RATCC  [27] NTRS  [28] RTTVQ  [29] RTTVC
  //   [30..33] PJSV px py pz E, the jet direction after the SV redefinition
  //   [34 .. 34+4*NTRS-1]  per rapidity track: INVTS (AABTAG track index),
  //   TGVT (rapidity), RTTVQI, RTTVCI
  // The ParticleID carries no particle relation: the object it would point
  // at is exactly the jet we are not emitting.
  edm4hep::ParticleIDCollection jetTags;
  // Largest X_jet per thrust hemisphere over the tagged jets, as PXBTAG
  // does for BTAG(29,30); NaN when the hemisphere has no tagged jet.
  float hemiBest[2] = {kNaN, kNaN};
  for (int j = 1; j <= njet; ++j) {
    // Collection position + 1 is the AABTAG jet number.
    const float cosjt = aa::PJET(1, j) * aa::PTHR(1) + aa::PJET(2, j) * aa::PTHR(2)
                      + aa::PJET(3, j) * aa::PTHR(3);
    const int hemi = cosjt >= 0.f ? 1 : 2;
    const int   jtag  = aa::JTAG(j);
    const float xeffj = aa::XEFFJ(j);
    if (jtag > 0 && (std::isnan(hemiBest[hemi - 1]) || xeffj > hemiBest[hemi - 1]))
      hemiBest[hemi - 1] = xeffj;

    auto tag = jetTags.create();
    tag.setAlgorithmType(kAlgoBtagCtr);
    tag.setType(jtag);
    tag.setLikelihood(xeffj);
    for (int i = 1; i <= 4; ++i) tag.addToParameters(aa::PJET(i, j));
    tag.addToParameters(static_cast<float>(jtag));
    tag.addToParameters(xeffj);
    tag.addToParameters(static_cast<float>(hemi));
    for (int i = 1; i <= aa::kNTagVars; ++i) tag.addToParameters(aa::TAGV (i, j));
    for (int i = 1; i <= aa::kNTagVars; ++i) tag.addToParameters(aa::RATVQ(i, j));
    for (int i = 1; i <= aa::kNTagVars; ++i) tag.addToParameters(aa::RATVC(i, j));
    tag.addToParameters(aa::RATCQ(j));
    tag.addToParameters(aa::RATCC(j));
    const int ntrs = std::clamp(aa::NTRS(j), 0, aa::kMaxRapTracks);
    tag.addToParameters(static_cast<float>(ntrs));
    tag.addToParameters(aa::RTTVQ(j));
    tag.addToParameters(aa::RTTVC(j));
    for (int i = 1; i <= 4; ++i) tag.addToParameters(aa::PJSV(i, j));
    for (int k = 1; k <= ntrs; ++k) {
      tag.addToParameters(static_cast<float>(aa::INVTS(k, j)));
      tag.addToParameters(aa::TGVT  (k, j));
      tag.addToParameters(aa::RTTVQI(k, j));
      tag.addToParameters(aa::RTTVCI(k, j));
    }
  }

  putParameter(bank, "CombinedTagEvent",      valid ? aa::XEFFEV() : kNaN, prov);
  putParameter(bank, "CombinedTagHemisphere", std::vector<float>{hemiBest[0], hemiBest[1]}, prov);
  putParameter(bank, "Oblateness",            valid ? aa::OBLVAL() : kNaN, prov);
  putParameter(bank, "NCombinedTagRows",            njet,      Provenance::Custom);
  putParameter(bank, "NSecondaryVertexHypotheses",  nhypo,     Provenance::Custom);
  putParameter(bank, "NSecondaryVertices",          nAccepted, Provenance::Custom);

  put(std::move(svCol),   bank, "SecondaryVertices", prov);
  put(std::move(jetTags), bank, "CombinedTagRow",    prov);
}

}  // namespace delphi_edm4hep::btag
