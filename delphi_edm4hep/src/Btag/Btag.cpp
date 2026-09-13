// Btag.cpp — b-tagging domain implementation.
//
// Reads the stored BTAG result directly from the DST and the recalculated
// event tag from the converter-owned BtagInfo service. The rich package
// output still comes from AAMAIN / AAMNVX (per-track impact parameters,
// probabilities, VD quality, and AABTAG's own primary vertex).

#include "delphi_edm4hep/Btag/Btag.h"

#include "delphi_edm4hep/Btag/BtagInfo.h"
#include "delphi_edm4hep/Event/EventInfo.h"
#include "delphi_edm4hep/internal/AabtagCommons.h"
#include "delphi_edm4hep/internal/AabtagStatus.h"
#include "delphi_edm4hep/internal/PaWalk.h"

#include <edm4hep/MutableParticleID.h>
#include <edm4hep/ParticleIDCollection.h>
#include <edm4hep/MutableVertex.h>
#include <edm4hep/VertexCollection.h>
#include <podio/UserDataCollection.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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

void BtagWriter::emitEventLevel(std::string_view bank, Provenance prov,
                               bool valid, const EventLevelTag& tag)
{
  const auto eventProb = [&](float value) { return valid ? prob(value) : kNaN; };
  // Index order within each triplet is (hemisphere 1, hemisphere 2, whole
  // event), matching QBTPRN/QBTPRP/QBTPRS(1..3).
  putParameter(bank, "ProbNegIP",
               std::vector<float>{eventProb(tag.probabilityNegative[0]),
                                  eventProb(tag.probabilityNegative[1]),
                                  eventProb(tag.probabilityNegative[2])}, prov);
  putParameter(bank, "ProbPosIP",
               std::vector<float>{eventProb(tag.probabilityPositive[0]),
                                  eventProb(tag.probabilityPositive[1]),
                                  eventProb(tag.probabilityPositive[2])}, prov);
  putParameter(bank, "ProbAllIP",
               std::vector<float>{eventProb(tag.probabilityAll[0]),
                                  eventProb(tag.probabilityAll[1]),
                                  eventProb(tag.probabilityAll[2])}, prov);
  // The thrust axis gets the same sentinel treatment: VFILL sets it to 2.0
  // as well, and a direction cosine can never legitimately exceed 1, so an
  // un-mapped 2.0 here would be a sentinel masquerading as data.
  putParameter(bank, "ThrustAxis",
               std::vector<float>{eventProb(tag.thrustAxis[0]),
                                  eventProb(tag.thrustAxis[1]),
                                  eventProb(tag.thrustAxis[2])}, prov);
  // QBTTHR(4) is the thrust VALUE, not an axis component.
  putParameter(bank, "ThrustValue",
               valid ? thrustValue(tag.thrustValue) : kNaN, prov);
}

void BtagWriter::emit()
{
  const int beamSpotError = event::current().beamSpot.errorCode;
  putParameter("BTAGCFG", "SourcePrefix",
               std::string(fromFullDst() ? "fDST" : "sDST"), Provenance::Custom);
  putParameter("BTAGCFG", "BeamSpotErrorCode", beamSpotError,
               Provenance::Derived);

  // AAFLAG is meaningful only when PSFBTG actually called AABTGS. PSFBTG
  // skips that call when IERRBS != 0 and leaves IBAD (and the rich COMMON
  // arrays) stale. Failed AABTAG events can retain derived values too, so gate
  // the entire rich payload on the combined current-event status rather than
  // sanitizing one field at a time.
  const auto status = aa::eventStatus(beamSpotError, aa::IBAD());
  const bool tagValid = status.valid;
  const auto& eventTags = current();

  // Both tags are emitted from the direct runtime snapshot. The stored tag
  // needs no validity gate: an absent bank retains the 2.0 sentinel, which
  // prob() maps to NaN.
  emitEventLevel("AABTAG", Provenance::Derived, tagValid,
                 eventTags.recalculated);
  emitEventLevel("BTG", Provenance::Transcribed, /*valid=*/true,
                 eventTags.stored);

  // The per-track arrays and AABTAG's vertex come from AAMAIN / AAMNVX.
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
}

}  // namespace delphi_edm4hep::btag
