// Bsaurus.cpp — BSAURUS inclusive B-reconstruction writer.
//
// Calls BSAURUS (bsaurus.car, dstana release 161018) once per event and copies
// its commons out. What the call does to the process, and how the writer
// undoes it:
//
//   * PXBTAG('RUN') re-runs AABTAG with BSAURUS's own steering (IFBFLA = 3,
//     IFK0ST = LSHORT, two BTAGAA passes) and leaves the AAFLAG common in that
//     state. IFBFLA = 3 changes BTAGAA's output on the NEXT event, so the whole
//     AAFLAG common is restored after the call. AADATA's second-call guard
//     makes BSAURUS's own initialisation call a no-op.
//   * BSAURUS, ELEPHANT, MUFLAG and AABTAG's MC smearing consume random
//     numbers from two generators (DELPHI's RNDM and JETSET's RLU) whose
//     state the next event's SKELANA/AABTAG processing depends on. BSAURUS
//     gets its own copy of both (context switch, below).
//   * PXLEID and FIXLAMB rewrite the mass code in the PA MAIN block of
//     identified leptons and anti-Lambdas, and ELEPHANT corrects photon
//     energies, in the ZEBRA banks of the current event. PHDST reads the next
//     event afresh, so this only matters within the event: the writer runs
//     last, after every bank reader.
//   * AABTAG's commons (AAMAIN, AASCND, AACTRS, ...) hold BSAURUS's re-run
//     afterwards, not PSFBTG's. BtagWriter has already read them.
//
// Output conventions follow Btag.cpp: lengths in mm in the typed collections,
// the -999 "not filled" sentinel mapped to NaN everywhere, counts as frame
// parameters. The word arrays keep BSAURUS's own units (cm, GeV, ps).

#include "delphi_edm4hep/Btag/Bsaurus.h"

#include "delphi_edm4hep/internal/AabtagCommons.h"
#include "delphi_edm4hep/internal/BsaurusCommons.h"
#include "delphi_edm4hep/internal/BsaurusWords.h"
#include "delphi_edm4hep/internal/PaWalk.h"

#include <edm4hep/MutableParticleID.h>
#include <edm4hep/MutableReconstructedParticle.h>
#include <edm4hep/MutableVertex.h>
#include <edm4hep/ParticleIDCollection.h>
#include <edm4hep/ReconstructedParticleCollection.h>
#include <edm4hep/VertexCollection.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace aa = delphi_edm4hep::aabtag;

// ---- Fortran state BSAURUS's call chain modifies ---------------------------
//
// These commons are read on the NEXT event by SKELANA, AABTAG, ELEPHANT or
// JETSET, and BSAURUS (or a package it drives) writes them. Each is
// snapshotted before CALL BSAURUS and put back afterwards, so every event that
// follows is bit-identical to a job that never called BSAURUS. Two flavours:
//
//   restored        the job's copy goes back after the call; BSAURUS sees the
//                   job's current values on entry (it needs some of them, e.g.
//                   SKELANA's per-event EBEAM in PXCONS);
//   context-switched BSAURUS keeps its own copy between events, swapped in
//                   before the call and saved after it, because BSAURUS sets
//                   the block up once (FORINI, YBINIT, PXBTAG's first call) and
//                   never again, and because a random stream should advance
//                   rather than be reset.
//
// The sizes are `nm -S` on the pinned release (dstana 161018 / jetset74); the
// blocks are declared opaque because only whole-block copies are needed.
//
//   PXCONS   (restored) BMAG, AMPI, PI, EBEAM, EBMDEF. ELEPHANT (AMPI =
//            0.1395679, BMAG = 1.213 ...), MAMMOTH (BMAG = 1.231) and PXDST set
//            their own values; SKELANA set AMPI = 0.139570 and PI = ACOS(-1.)
//            in PSINI and computes VECP energies and STIC angles with them.
//            Left changed, particle energies move by one float ulp from the
//            next event on. BSAURUS reads SKELANA's EBEAM from it (SHEM,
//            EHEM_REF), hence restored rather than switched.
//   PXPCON   (restored) ELEPHANT's year / flags / vertex state, shared with
//            the electron ID SKELANA runs itself.
//   VDBMSP, VDDSKF  (restored) the VD beam-spot package: ELEPHANT's
//            initialisation calls VDIDST and SETBS with its own smearing
//            widths, and on simulation VDMCBS smears the generated vertex with
//            those widths for SKELANA's beam spot on the next event.
//   LUDAT2, LUJETS  (restored) JETSET particle data and event record.
//   AACBSD, AARND, AAFLAG  (restored) AABTAG beam-spot constants, random-init
//            flag and steering (PXBTAG('RUN') leaves IFBFLA = 3, which makes
//            BTAGAA substitute 0.6 for the negative-side probability).
//   ELOUTP   (restored) ELEPHANT's per-event output.
//   MAMCOM   (switched) MAMMOTH steering. SKELANA's V0 reconstruction
//            (MAMDATA, L_RECV0) and BSAURUS's FORINI (L_OWNPV, L_IDVD, ...)
//            each configure it once.
//   LUDAT1   (switched) JETSET steering. BSAURUS sets MSTU(12) = 0 and
//            MSTU(22) = 10000 once and the LUCLUS parameters per event; the
//            error limit must stay raised in BSAURUS's view or JETSET stops
//            the job after ten LUERRM errors.
//   LUDATR   (switched) JETSET's RLU state. LUTHRU draws the SIGN of the
//            thrust axis from RLU, so sharing the stream mirrors AABTAG's
//            thrust axis on the next event and swaps its hemisphere labels.
//   SXCRNU   (switched) the state of DELPHI's RNDM (dstana's RNDM is SXRNU,
//            RANNOR is SXRVG). ELEPHANT, MUFLAG, AABEAM and AABTAG's MC
//            impact-parameter smearing draw from it. Giving BSAURUS its own
//            stream is used rather than a CERNLIB RDMOUT / RDMIN bracket:
//            dstana's RDMOUT has a single save slot and would reset BSAURUS's
//            stream to the same point on every event.
extern "C" {
  extern unsigned char pxcons_[20];
  extern unsigned char pxpcon_[76];
  extern unsigned char vdbmsp_[1120064];
  extern unsigned char vddskf_[580];
  extern unsigned char ludat2_[22064];
  extern unsigned char lujets_[240004];
  extern unsigned char aacbsd_[320];
  extern unsigned char aarnd_[4];
  extern unsigned char eloutp_[220];
  extern unsigned char mamcom_[128];
  extern unsigned char ludat1_[3200];
  extern unsigned char ludatr_[424];
  extern unsigned char sxcrnu_[4624];
}

namespace delphi_edm4hep::bsaurus {

namespace {

constexpr double kCm2Mm    = 10.0;
constexpr float  kCm2Mm2_f = 100.f;
constexpr float  kNaN      = std::numeric_limits<float>::quiet_NaN();

// Vertex algorithmType for BSAURUS's fitted B decay vertex. Distinct from
// Vertex.cpp (0, 1, 2, 4, 10, 11) and Btag.cpp (3, 5).
constexpr int kAlgoBsaurusSV = 12;
// ParticleID algorithmTypes of the two word collections.
constexpr int kAlgoHemisphereWords = 60;
constexpr int kAlgoTrackWords      = 61;

// ISAURUS value from which the secondary-vertex words are meaningful.
constexpr int kStatusVertexFit = 4;

bool  filled(float v) { return v != kSentinel; }
float word(float v)   { return filled(v) ? v : kNaN; }

// mm from a cm word; NaN stays NaN.
float mm(float cm_word) { return static_cast<float>(word(cm_word) * kCm2Mm); }

struct Block { void* data; std::size_t size; };

// Restored to the job's state after the call.
const Block kRestored[] = {
  {pxcons_, sizeof pxcons_}, {pxpcon_, sizeof pxpcon_},
  {vdbmsp_, sizeof vdbmsp_}, {vddskf_, sizeof vddskf_},
  {ludat2_, sizeof ludat2_}, {lujets_, sizeof lujets_},
  {aacbsd_, sizeof aacbsd_}, {aarnd_,  sizeof aarnd_},
  {eloutp_, sizeof eloutp_}, {&aa::aaflag_, sizeof aa::aaflag_},
};
// Context-switched: BSAURUS keeps its own copy between events.
const Block kSwitched[] = {
  {mamcom_, sizeof mamcom_}, {ludat1_, sizeof ludat1_},
  {ludatr_, sizeof ludatr_}, {sxcrnu_, sizeof sxcrnu_},
};

std::size_t total(const Block* begin, const Block* end) {
  std::size_t n = 0;
  for (const Block* b = begin; b != end; ++b) n += b->size;
  return n;
}

// Saves the job's copy of every block on construction and puts it back on
// destruction; swaps BSAURUS's own copy of the switched blocks in between.
// BSAURUS's copies live for the process, like PHDST itself: the first call
// starts from the job's state, later calls from where BSAURUS left off.
class FortranStateGuard {
public:
  FortranStateGuard()
    : job_(total(std::begin(kRestored), std::end(kRestored))
         + total(std::begin(kSwitched), std::end(kSwitched))) {
    unsigned char* out = job_.data();
    for (const Block& b : kRestored) { std::memcpy(out, b.data, b.size); out += b.size; }
    for (const Block& b : kSwitched) { std::memcpy(out, b.data, b.size); out += b.size; }
    if (!bsaurus_.empty()) {
      const unsigned char* in = bsaurus_.data();
      for (const Block& b : kSwitched) { std::memcpy(b.data, in, b.size); in += b.size; }
    }
  }
  ~FortranStateGuard() {
    bsaurus_.resize(total(std::begin(kSwitched), std::end(kSwitched)));
    unsigned char* out = bsaurus_.data();
    for (const Block& b : kSwitched) { std::memcpy(out, b.data, b.size); out += b.size; }
    const unsigned char* in = job_.data();
    for (const Block& b : kRestored) { std::memcpy(b.data, in, b.size); in += b.size; }
    for (const Block& b : kSwitched) { std::memcpy(b.data, in, b.size); in += b.size; }
  }
  FortranStateGuard(const FortranStateGuard&) = delete;
  FortranStateGuard& operator=(const FortranStateGuard&) = delete;

private:
  std::vector<unsigned char>        job_;       // every block, as found before the call
  static std::vector<unsigned char> bsaurus_;   // the switched blocks, as BSAURUS left them
};
std::vector<unsigned char> FortranStateGuard::bsaurus_;

}  // namespace

void BsaurusWriter::emit()
{
  const std::string_view bank = "BSAURUS";
  const Provenance prov = Provenance::Derived;

  // ---- run the package inside its own Fortran state --------------------------
  {
    FortranStateGuard guard;
    bsaurus_();
  }

  // Slot names, once per job (the registry keeps the first registration).
  putMetadata(bank, "EventWordNames",      words::expand(words::kEvent,      kNEventWords));
  putMetadata(bank, "HemisphereWordNames", words::expand(words::kHemisphere, kNHemWords));
  putMetadata(bank, "TrackWordNames",      words::expand(words::kTrack,      kNTrackWords));

  // ---- event level ----------------------------------------------------------
  const int status[2] = {ISAURUS(1), ISAURUS(2)};
  putParameter(bank, "Status", std::vector<int>{status[0], status[1]}, prov);

  std::vector<float> eventWords(kNEventWords);
  for (int k = 1; k <= kNEventWords; ++k) eventWords[k - 1] = word(BSEVT(k));
  putParameter(bank, "EventWords", std::move(eventWords), prov);

  // FORVEC resets NTOT on every event it processes; the per-particle words
  // are only defined for rows 1..NTOT.
  const int ntot = std::clamp(NTOT(), 0, kMaxParticles);
  const int nch  = std::clamp(NCH(),  0, ntot);
  putParameter(bank, "NParticles", ntot, Provenance::Custom);
  putParameter(bank, "NCharged",   nch,  Provenance::Custom);
  // Index of the cluster chosen as the B candidate in BSAURUS's own LUCLUS
  // list (1 = most energetic), defined once the rapidity algorithm has run
  // (Status >= 2).
  const bool chosen = status[0] >= 2 && status[1] >= 2;
  putParameter(bank, "HemisphereClusterIndex",
               std::vector<int>{chosen ? HBCLUS(1) : 0,
                                chosen ? HBCLUS(2) : 0}, prov);

  // lpa -> PA-walk index, so LPAVEC (a ZEBRA L-address) resolves to the
  // particle Tracking emitted for that PA.
  std::unordered_map<int, int> lpa_to_pa;
  pawalk::forEachPA([&](int lpa, int paIdx) { lpa_to_pa.emplace(lpa, paIdx); });
  const auto particleForRow =
    [&](int i) -> std::optional<edm4hep::ReconstructedParticle> {
      const auto it = lpa_to_pa.find(LPAVEC(i));
      if (it == lpa_to_pa.end()) return std::nullopt;
      return particleForPa(it->second);
    };

  // ---- hemispheres ----------------------------------------------------------
  edm4hep::ReconstructedParticleCollection hemispheres;
  edm4hep::VertexCollection                vertices;
  edm4hep::ParticleIDCollection            hemisphereWords;
  edm4hep::ParticleIDCollection            trackWords;

  std::vector<edm4hep::MutableReconstructedParticle> hemi;
  std::vector<std::optional<edm4hep::MutableVertex>> sv(2);

  for (int ih = 1; ih <= 2; ++ih) {
    // EDM4hep 1.0 has no `type` on ReconstructedParticle; the hemisphere status
    // is the `type` of the HemisphereWords row and the Status parameter.
    auto h = hemispheres.create();

    // The 'optimal' B 4-vector (BTT). NaN where the hemisphere was not
    // reconstructed, rather than a zero that reads as a measurement.
    const float px = word(BSHEM(words::IBH_BTT,     ih));
    const float py = word(BSHEM(words::IBH_BTT + 1, ih));
    const float pz = word(BSHEM(words::IBH_BTT + 2, ih));
    const float e  = word(BSHEM(words::IBH_BTT + 3, ih));
    h.setMomentum({px, py, pz});
    h.setEnergy(e);
    const float m2 = e * e - (px * px + py * py + pz * pz);
    h.setMass(std::isnan(m2) ? kNaN : std::sqrt(std::max(0.f, m2)));
    // Vertex charge as the plain sum of the charges of the tracks at the
    // secondary vertex (RAWQ). The net-based estimate is in the words (QVER).
    h.setCharge(word(BSHEM(words::IBH_RAWQ, ih)));

    // The fitted B decay vertex, where the fit ran. CYBFIT declares VCHI2 /
    // NDOF but BSAURUS never fills them, so chi2 is NaN and ndf 0; the fit
    // probability (PROB) is the quality measure, in parameters[1].
    const bool haveSV = status[ih - 1] >= kStatusVertexFit
                     && filled(BSHEM(words::IBH_SV,     ih))
                     && filled(BSHEM(words::IBH_SV + 1, ih))
                     && filled(BSHEM(words::IBH_SV + 2, ih));
    if (haveSV) {
      auto v = vertices.create();
      v.setSecondary(true);
      v.setAlgorithmType(kAlgoBsaurusSV);
      const edm4hep::Vector3f pos{mm(BSHEM(words::IBH_SV,     ih)),
                                  mm(BSHEM(words::IBH_SV + 1, ih)),
                                  mm(BSHEM(words::IBH_SV + 2, ih))};
      v.setPosition(pos);
      // FORVTX packs the covariance like AABTAG and EDM4hep: xx xy yy xz yz zz.
      v.setCovMatrix({word(BSHEM(words::IBH_SC,     ih)) * kCm2Mm2_f,
                      word(BSHEM(words::IBH_SC + 1, ih)) * kCm2Mm2_f,
                      word(BSHEM(words::IBH_SC + 2, ih)) * kCm2Mm2_f,
                      word(BSHEM(words::IBH_SC + 3, ih)) * kCm2Mm2_f,
                      word(BSHEM(words::IBH_SC + 4, ih)) * kCm2Mm2_f,
                      word(BSHEM(words::IBH_SC + 5, ih)) * kCm2Mm2_f});
      v.setChi2(kNaN);
      v.setNdf(0);
      // parameters: [0] hemisphere  [1] fit probability  [2] r-phi decay
      // length (mm)  [3] its error  [4] 3-D decay length (mm)  [5] its error
      // [6] sigma(phi), [7] sigma(theta) of the flight direction (rad)
      // [8] tracks at the vertex (NACC)
      v.addToParameters(static_cast<float>(ih));
      v.addToParameters(word(BSHEM(words::IBH_PROB,    ih)));
      v.addToParameters(mm  (BSHEM(words::IBH_DEC,     ih)));
      v.addToParameters(mm  (BSHEM(words::IBH_DECS,    ih)));
      v.addToParameters(mm  (BSHEM(words::IBH_DEC3D,   ih)));
      v.addToParameters(mm  (BSHEM(words::IBH_DEC3DS,  ih)));
      v.addToParameters(word(BSHEM(words::IBH_SIGPHI,  ih)));
      v.addToParameters(word(BSHEM(words::IBH_SIGTHET, ih)));
      v.addToParameters(word(BSHEM(words::IBH_NACC,    ih)));
      h.setDecayVertex(v);
      h.setReferencePoint(pos);
      sv[ih - 1] = v;
    }

    auto w = hemisphereWords.create();
    w.setAlgorithmType(kAlgoHemisphereWords);
    w.setType(status[ih - 1]);
    w.setLikelihood(word(BSHEM(words::IBH_FLAV45, ih)));
    for (int k = 1; k <= kNHemWords; ++k) w.addToParameters(word(BSHEM(k, ih)));
    w.setParticle(h);

    hemi.push_back(h);
  }

  // ---- particles ------------------------------------------------------------
  // One row per FORVEC particle, in its order (charged by descending momentum,
  // then neutrals), so BSAURUS's internal index (the BSAU word) is row + 1.
  for (int i = 1; i <= ntot; ++i) {
    auto t = trackWords.create();
    t.setAlgorithmType(kAlgoTrackWords);
    const int ih = static_cast<int>(std::lround(BSPAR(words::IBP_HEM, i)));
    t.setType(ih);
    t.setLikelihood(word(BSPAR(words::IBP_NET, i)));
    for (int k = 1; k <= kNTrackWords; ++k) t.addToParameters(word(BSPAR(k, i)));

    const auto particle = particleForRow(i);
    if (!particle) continue;
    t.setParticle(*particle);
    // Tracks in the secondary-vertex fit: the B candidate's constituents, as
    // a relation on both the hemisphere and its vertex.
    if ((ih == 1 || ih == 2) && BSPAR(words::IBP_SV, i) == 1.f) {
      hemi[ih - 1].addToParticles(*particle);
      if (sv[ih - 1]) sv[ih - 1]->addToParticles(*particle);
    }
  }

  put(std::move(vertices),        bank, "SecondaryVertices", prov);
  put(std::move(hemispheres),     bank, "Hemispheres",       prov);
  put(std::move(hemisphereWords), bank, "HemisphereWords",   prov);
  put(std::move(trackWords),      bank, "TrackWords",        prov);
}

}  // namespace delphi_edm4hep::bsaurus
