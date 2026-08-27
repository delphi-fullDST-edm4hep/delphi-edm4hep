// Btag.cpp — b-tagging domain implementation.
//
// Reads PSCBTG (event/hemisphere probabilities) and, when AABTAG was
// actually rerun, the AAMAIN / AAMNVX commons (per-track impact
// parameters, per-track probabilities, VD quality, and AABTAG's own
// primary vertex).

#include "delphi_edm4hep/Btag/Btag.h"

#include "delphi_edm4hep/internal/AabtagCommons.h"
#include "delphi_edm4hep/internal/AabtagStatus.h"
#include "delphi_edm4hep/internal/BtagProvenance.h"
#include "delphi_edm4hep/internal/PaWalk.h"

#include "skelana/pscbsp.hpp"
#include "skelana/pscbtg.hpp"
#include "skelana/pscflg.hpp"

#include <edm4hep/VertexCollection.h>
#include <podio/UserDataCollection.h>

#include <algorithm>
#include <cstdint>
#include <limits>
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

void BtagWriter::emit()
{
  // Record the b-tag mode unconditionally, so a consumer can distinguish a
  // no-b-tag file from one where AABTAG simply produced nothing without
  // guessing from which collections happen to be present. The separate
  // --btag-pv compatibility switch is documented by the CLI; it does not
  // change which AABTAG collections are emitted.
  putParameter("BTAGCFG", "Mode",
               std::string(mode_ == BtagMode::Off    ? "off"
                         : mode_ == BtagMode::Bank   ? "bank"
                                                     : "recalc"));
  putParameter("BTAGCFG", "Recalculated", recalculated() ? 1 : 0);
  // These fields deliberately live under the writer's source prefix. Pass 2
  // carries the copied sDST_EVT_* identity parameters but has no fDST_EVT_*
  // domain, so sDST_EVT_BeamSpotErrorCode is not valid evidence for which
  // beamspot status governed the fDST AABTAG invocation. Serialize the live
  // current-pass value beside the b-tag payload instead.
  putParameter("BTAGCFG", "SourcePrefix", std::string(source_tag_));
  putParameter("BTAGCFG", "BeamSpotErrorCode", sk::IERRBS);
  putParameter("BTAGCFG", "PrimaryVertexPolicy",
               std::string(provenance::primaryVertexPolicy(sk::IFLPVT)));
  // Retain the raw steering word as well as the stable semantic label. This
  // makes the legacy --btag-pv compatibility mode auditable without forcing
  // downstream code to know the Fortran flag convention.
  putParameter("BTAGCFG", "IFLPVT", sk::IFLPVT);

  if (mode_ == BtagMode::Off) return;

  const std::string_view bank = mnemonic();
  const bool reran = recalculated();
  // AAFLAG is meaningful only when PSFBTG actually called AABTGS. PSFBTG
  // skips that call when IERRBS != 0 and leaves IBAD (and the rich COMMON
  // arrays) stale. Failed AABTAG events can retain derived values too, so gate
  // the entire rich payload on the combined current-event status rather than
  // sanitizing one field at a time.
  const auto status = aa::eventStatus(sk::IERRBS, reran ? aa::IBAD() : 0);
  const bool tagValid = !reran || status.valid;
  const auto eventProb = [&](float value) {
    return tagValid ? prob(value) : kNaN;
  };

  // ---- Event / hemisphere probabilities (PSCBTG; both modes) ----------
  // Index order within each triplet is (hemisphere 1, hemisphere 2, whole
  // event), matching QBTPRN/QBTPRP/QBTPRS(1..3).
  putParameter(bank, "ProbNegIP",
               std::vector<float>{eventProb(sk::QBTPRN(1)),
                                  eventProb(sk::QBTPRN(2)),
                                  eventProb(sk::QBTPRN(3))});
  putParameter(bank, "ProbPosIP",
               std::vector<float>{eventProb(sk::QBTPRP(1)),
                                  eventProb(sk::QBTPRP(2)),
                                  eventProb(sk::QBTPRP(3))});
  putParameter(bank, "ProbAllIP",
               std::vector<float>{eventProb(sk::QBTPRS(1)),
                                  eventProb(sk::QBTPRS(2)),
                                  eventProb(sk::QBTPRS(3))});
  // The thrust axis gets the same sentinel treatment: VFILL sets it to 2.0
  // as well, and a direction cosine can never legitimately exceed 1, so an
  // un-mapped 2.0 here would be a sentinel masquerading as data. (Caught by
  // running --btag bank on a real short DST, where the BTAG bank is absent
  // and every PSCBTG word is left at 2.0.)
  putParameter(bank, "ThrustAxis",
               std::vector<float>{eventProb(sk::QBTTHR(1)),
                                  eventProb(sk::QBTTHR(2)),
                                  eventProb(sk::QBTTHR(3))});
  // QBTTHR(4) is the thrust VALUE, not an axis component. (delphi-nanoaod
  // drops it; we keep it -- it is free and the axis alone is not enough to
  // reproduce a thrust-based hemisphere split.)
  putParameter(bank, "ThrustValue",
               tagValid ? thrustValue(sk::QBTTHR(4)) : kNaN);

  if (!reran) return;

  // ---- AABTAG primary-vertex output (AAMNVX) -------------------------
  // Emitted as its own collection rather than replacing the DELANA PV.
  // The collection is empty when Valid != 1. A nonempty entry still
  // needs NDF/NTracksAttached checks before it is described as track-fitted;
  // a status-zero result can be a beamspot-only constraint.
  // With IFLPVT = Keep (the default) SKELANA never overwrites QVTX, so a
  // consumer gets both vertices and picks; nothing is destroyed.
  edm4hep::VertexCollection btagPv;
  if (tagValid) {
    auto pv = btagPv.create();
    pv.setPrimary(true);
    pv.setAlgorithmType(kAlgoBtagPV);
    pv.setPosition({static_cast<float>(aa::POSVX(1) * kCm2Mm),
                    static_cast<float>(aa::POSVX(2) * kCm2Mm),
                    static_cast<float>(aa::POSVX(3) * kCm2Mm)});
    pv.setChi2(aa::CHI2VX());
    pv.setNdf(aa::NDOFVX());
    pv.setCovMatrix({aa::COVVX(1) * kCm2Mm2_f, aa::COVVX(2) * kCm2Mm2_f,
                     aa::COVVX(3) * kCm2Mm2_f, aa::COVVX(4) * kCm2Mm2_f,
                     aa::COVVX(5) * kCm2Mm2_f, aa::COVVX(6) * kCm2Mm2_f});
  }
  put(std::move(btagPv), bank, "PrimaryVertex");

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
  putParameter(bank, "BadEventCode",     status.badEventCode);
  putParameter(bank, "AlgorithmInvoked", status.algorithmInvoked ? 1 : 0);
  putParameter(bank, "Valid",            status.valid ? 1 : 0);
  putParameter(bank, "NTracksRaw",       std::clamp(ntrk_raw, 0, aa::kMaxTracks));
  putParameter(bank, "NTracks",         ntrk);
  putParameter(bank, "NTracksAttached", tagValid ? aa::NATTVX() : 0);
  // Retain the established field name for campaign compatibility. Its value
  // is deliberately conservative: 1 means the common reached capacity and
  // additional eligible tracks may (but cannot be proven to) have been lost.
  putParameter(bank, "Truncated",
               tagValid && ntrk_raw >= aa::kMaxTracks ? 1 : 0);

  // lpa -> PA-walk index, so IADTR (a ZEBRA L-address) can be resolved to
  // the Particle the Tracking domain emitted for that PA.
  std::unordered_map<int, int> lpa_to_pa;
  pawalk::forEachPA([&](int lpa, int paIdx) { lpa_to_pa.emplace(lpa, paIdx); });

  // Pass 1 has TrackingWriter's direct PA -> emitted-particle map. Pass 2
  // instead has MatchProvenanceWriter's fDST PA -> sDST particle map; the
  // fDST_MAIN collection is a one-to-one clone, so those indices are also the
  // correct fDST_MAIN indices. Without this fallback every pass-2 index was
  // silently written as -1.
  const std::vector<int>* pa_to_particle = nullptr;
  if (ctx_.tracking) {
    pa_to_particle = &ctx_.tracking->pa_to_particle;
  } else if (ctx_.fdst_pa_to_sdst_particle) {
    pa_to_particle = &*ctx_.fdst_pa_to_sdst_particle;
  }

  podio::UserDataCollection<std::int32_t> particleIdx;
  podio::UserDataCollection<float>        impRPhi, impRPhiErr;
  podio::UserDataCollection<float>        impZ,    impZErr;
  podio::UserDataCollection<float>        probRPhi, probZ;
  podio::UserDataCollection<std::int32_t> usedForTag, attachedToPv;
  podio::UserDataCollection<std::int32_t> nHitsRPhi, nHitsZ;
  podio::UserDataCollection<std::int32_t> nLayersRPhi, nLayersZ;
  podio::UserDataCollection<float>        chi2Vd, chi2Pv, momentum;

  for (int i = 1; i <= ntrk; ++i) {
    int p_idx = -1;
    if (pa_to_particle) {
      if (auto it = lpa_to_pa.find(aa::IADTR(i)); it != lpa_to_pa.end()) {
        const int paIdx = it->second;
        if (paIdx >= 0 && paIdx < static_cast<int>(pa_to_particle->size())) {
          p_idx = (*pa_to_particle)[paIdx];
        }
      }
    }
    particleIdx.push_back(p_idx);

    // PARIMP is the SIGNED r-phi impact parameter wrt AABTAG's POSVX, in
    // DELPHI cm and DELPHI sign convention -- NOT the LCIO D0 sign used by
    // the Track collections. Converted to mm, sign left as AABTAG set it,
    // because the sign is the physics here (the negative-IP side is the
    // mistag control sample). Do not mix with sDST_TRAC_d0PV or
    // sDST_PV_trackD0PV; see Vertex.cpp / Tracking.cpp on those.
    impRPhi   .push_back(static_cast<float>(aa::PARIMP(i) * kCm2Mm));
    impRPhiErr.push_back(static_cast<float>(aa::SIGIMP(i) * kCm2Mm));
    impZ      .push_back(static_cast<float>(aa::EZED  (i) * kCm2Mm));
    impZErr   .push_back(static_cast<float>(aa::SIGZED(i) * kCm2Mm));

    // AATPRB leaves these at 1.0 for tracks it could not use.
    probRPhi.push_back(aa::TRPR (i));
    probZ   .push_back(aa::TRPRZ(i));

    usedForTag  .push_back(aa::ISRT(i));      // 0 = not used, > 0 = used
    attachedToPv.push_back(aa::INMVX(i) ? 1 : 0);
    // These count-like legacy values are signed: AAP* efficiency/acceptance
    // corrections negate them to mark rejection; abs(value) is the count.
    nHitsRPhi   .push_back(aa::NVDP (i));
    nHitsZ      .push_back(aa::NVDPZ(i));
    nLayersRPhi .push_back(aa::NLAY (i));
    nLayersZ    .push_back(aa::NLAYZ(i));
    chi2Vd      .push_back(aa::CHI2VD(i));
    chi2Pv      .push_back(aa::CHI2TR(i));
    momentum    .push_back(aa::PMOM  (i));
  }

  put(std::move(particleIdx),  bank, "Tracks_ParticleIndex");
  put(std::move(impRPhi),      bank, "Tracks_ImpactParRPhi");
  put(std::move(impRPhiErr),   bank, "Tracks_ImpactParRPhiError");
  put(std::move(impZ),         bank, "Tracks_ImpactParZ");
  put(std::move(impZErr),      bank, "Tracks_ImpactParZError");
  put(std::move(probRPhi),     bank, "Tracks_ProbRPhi");
  put(std::move(probZ),        bank, "Tracks_ProbZ");
  put(std::move(usedForTag),   bank, "Tracks_UsedForTag");
  put(std::move(attachedToPv), bank, "Tracks_AttachedToPV");
  put(std::move(nHitsRPhi),    bank, "Tracks_NVDHitsRPhi");
  put(std::move(nHitsZ),       bank, "Tracks_NVDHitsZ");
  put(std::move(nLayersRPhi),  bank, "Tracks_NVDLayersRPhi");
  put(std::move(nLayersZ),     bank, "Tracks_NVDLayersZ");
  put(std::move(chi2Vd),       bank, "Tracks_Chi2VD");
  put(std::move(chi2Pv),       bank, "Tracks_Chi2PV");
  put(std::move(momentum),     bank, "Tracks_Momentum");
}

}  // namespace delphi_edm4hep::btag
