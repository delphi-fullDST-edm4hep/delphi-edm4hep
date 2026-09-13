// Vertex domain — implementation S4.
//
// Reads the raw reco/simulation PV chains, reconstructed-V0 bank and photon
// conversion links plus the direct beamspot state. All positions are converted
// from DELPHI cm to EDM4hep mm. No SKELANA standard COMMON is required.

#include "delphi_edm4hep/Vertex/Vertex.h"

#include "delphi_edm4hep/Event/EventInfo.h"

#include "phdst/uxcom.hpp"   // ph::LQ, ph::IQ, ph::Q  (raw ZEBRA store)
#include "phdst/uxlink.hpp"  // ph::LDTOP

#include <edm4hep/MutableVertex.h>
#include <edm4hep/VertexCollection.h>
#include <edm4hep/TrackCollection.h>
#include <edm4hep/TrackState.h>
#include <podio/Frame.h>
#include <podio/UserDataCollection.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ph = phdst;

namespace delphi_edm4hep::vertex {

namespace {

// DELPHI cm -> EDM4hep mm conversion. cm^2 -> mm^2 picks up an x100
// factor; we precompute it once.
constexpr double kCm2Mm    = 10.0;
constexpr float  kCm2Mm2_f = static_cast<float>(kCm2Mm * kCm2Mm);

// Algorithm-type tags on edm4hep::Vertex. Arbitrary but stable across
// the library; downstream consumers can branch on these.
constexpr int kAlgoPrimary    = 0;
// Any reconstructed-chain entry that is not the event primary, including a
// dummy beamspot bucket. The raw status bits distinguish dummy/secondary
// semantics; algorithmType intentionally makes only the primary/non-primary
// distinction.
constexpr int kAlgoNonPrimaryReco = 1;
constexpr int kAlgoBeamSpot   = 2;
constexpr int kAlgoSimulation = 4;
constexpr int kAlgoV0         = 10;
constexpr int kAlgoPhotonConv = 11;

// DELPHI LPV status bits from IQ(lpv). Bit 1 marks a dummy vertex and bit 2 a
// secondary vertex. A dummy first slot is the unfitted beam-spot bucket and
// must never be published as the event PV. PSHVTX retained the low five bits,
// so the direct decoder deliberately applies the same mask.
constexpr std::int32_t kStatusDummy     = 0x01;
constexpr std::int32_t kStatusSecondary = 0x02;
constexpr int kMaxRecoVertices = 150;  // DELPHI NVTXMX cap

constexpr bool isPrimaryRecoVertex(bool first, std::int32_t status) {
  return first &&
         (status & (kStatusDummy | kStatusSecondary)) == 0;
}

static_assert(isPrimaryRecoVertex(true, 0));
static_assert(!isPrimaryRecoVertex(true, kStatusDummy));
static_assert(!isPrimaryRecoVertex(true, kStatusSecondary));
static_assert(!isPrimaryRecoVertex(false, 0));

// Fill a Vertex from the raw ZEBRA vertex bank at offset lpv (the
// LQ(LDTOP-1) chain). Layout mirrors delphi-raw-nanoaod's fillVtx:
// Q(lpv+5..7) = position cm,
// Q(lpv+8) = chi2, IQ(lpv+3) = ndf, Q(lpv+9..14) = error matrix.
void fillFromLDTOP(edm4hep::MutableVertex vtx, int lpv) {
  vtx.setPosition({
    static_cast<float>(ph::Q(lpv + 5) * kCm2Mm),
    static_cast<float>(ph::Q(lpv + 6) * kCm2Mm),
    static_cast<float>(ph::Q(lpv + 7) * kCm2Mm),
  });
  vtx.setChi2(ph::Q(lpv + 8));
  vtx.setNdf (ph::IQ(lpv + 3));
  vtx.setCovMatrix({
    ph::Q(lpv +  9) * kCm2Mm2_f,   // XX
    ph::Q(lpv + 10) * kCm2Mm2_f,   // XY
    ph::Q(lpv + 11) * kCm2Mm2_f,   // YY
    ph::Q(lpv + 12) * kCm2Mm2_f,   // XZ
    ph::Q(lpv + 13) * kCm2Mm2_f,   // YZ
    ph::Q(lpv + 14) * kCm2Mm2_f,   // ZZ
  });
}

void fillSimulationVertex(edm4hep::MutableVertex vtx, int lpv, bool compact) {
  const int xyz = compact ? lpv + 4 : lpv + 5;
  vtx.setPosition({ph::Q(xyz) * static_cast<float>(kCm2Mm),
                   ph::Q(xyz + 1) * static_cast<float>(kCm2Mm),
                   ph::Q(xyz + 2) * static_cast<float>(kCm2Mm)});
  vtx.setChi2(0.f);
  vtx.setNdf(0);
  vtx.setCovMatrix({0.f, 0.f, 0.f, 0.f, 0.f, 0.f});
}

}  // namespace

void VertexWriter::emit()
{
  // Need tracking output for V0 / PhotonConv -> Particle relations.
  if (!ctx_.tracking) {
    // Still emit empty collections so downstream schema is stable.
    put(edm4hep::VertexCollection{}, "PV", "PrimaryVertex",
        Provenance::Transcribed);
    put(edm4hep::VertexCollection{}, "PV", "Vertices",
        Provenance::Transcribed);
    put(podio::UserDataCollection<std::int32_t>{}, "PV", "Vertices_StatusBits",
        Provenance::Transcribed);
    put(edm4hep::VertexCollection{}, "BSP", "BeamSpot", Provenance::Derived);
    put(edm4hep::VertexCollection{}, "V0", "V0Candidates",
        Provenance::Transcribed);
    put(edm4hep::VertexCollection{}, "PHC", "PhotonConversions",
        Provenance::Transcribed);
    return;
  }
  const auto& tracking = *ctx_.tracking;

  edm4hep::VertexCollection             pvCol;
  edm4hep::VertexCollection             vtxCol;
  podio::UserDataCollection<std::int32_t> statusBits;
  edm4hep::VertexCollection             bspCol;
  edm4hep::VertexCollection             v0Col;
  edm4hep::VertexCollection             phcCol;

  auto add_particle_by_lpa = [&](edm4hep::MutableVertex vtx, int lpa) {
    const auto found = tracking.lpa_to_particle.find(lpa);
    if (found == tracking.lpa_to_particle.end()) return;
    vtx.addToParticles(tracking.particle_handles[found->second]);
  };

  // Reco vertices captured in LPV-chain order plus a handle to the standalone
  // PrimaryVertex copy, so outgoing-particle assignments can be wired after
  // creation.
  std::vector<edm4hep::MutableVertex> recoVtx;
  edm4hep::MutableVertex pvHandle{};
  bool havePvHandle = false;

  // ----- Reconstructed PV chain -----
  // Convention: entry j=1 is the primary vertex unless status bit 1 (dummy)
  // or bit 2 (secondary) is set. The full chain, including dummy entries, is
  // also emitted into `Vertices` with the raw per-entry status bits.
  //
  // As in PSHVTX, only IQ(LPV)'s low five vertex-status bits are published;
  // higher PXTAG candidate bits are outside this collection's contract.
  // Primary-vertex position in DELPHI cm, captured for the d0PV/z0PV
  // impact computation below. Stays sentinel if no usable PV is found.
  double pv_x_cm = -999.0, pv_y_cm = -999.0, pv_z_cm = -999.0;
  bool   have_pv = false;

  int lpv = (ph::LDTOP > 0) ? ph::LQ(ph::LDTOP - 1) : 0;
  int count = 0;
  while (lpv > 0 && count < kMaxRecoVertices) {
    // PSHVTX retained only the low five status bits in KVTX(17,*).
    const std::int32_t status = ph::IQ(lpv) & 0x1f;
    const bool is_primary = isPrimaryRecoVertex(count == 0, status);

    auto vtx = vtxCol.create();
    vtx.setPrimary(is_primary);
    vtx.setAlgorithmType(is_primary ? kAlgoPrimary : kAlgoNonPrimaryReco);
    fillFromLDTOP(vtx, lpv);
    statusBits.push_back(status);
    recoVtx.push_back(vtx);

    if (is_primary) {
      auto pv = pvCol.create();
      pv.setPrimary(true);
      pv.setAlgorithmType(kAlgoPrimary);
      fillFromLDTOP(pv, lpv);
      pv_x_cm = ph::Q(lpv + 5);
      pv_y_cm = ph::Q(lpv + 6);
      pv_z_cm = ph::Q(lpv + 7);
      have_pv = true;
      pvHandle = pv;
      havePvHandle = true;
    }
    ++count;
    lpv = ph::LQ(lpv);
  }

  // ----- Wire each reco vertex -> its outgoing-particle assignment -----
  // DELPHI documents the PA sub-chain at LQ(lpv-1) as outgoing particles,
  // not as the tracks used by the vertex fit. Keep that faithful meaning here;
  // AABTAG fit membership is published separately by the Btag writer. The
  // running pa_idx matches tracking.pa_to_particle (built by the same
  // forEachPA walk), so each PA maps to its emitted Particle.
  {
    const auto& pa2p = tracking.pa_to_particle;
    int pa_idx = 0;
    const int ldtop = ph::LDTOP;
    std::vector<int> lpvChain;
    for (int lpv = (ldtop > 0) ? ph::LQ(ldtop - 1) : 0;
         lpv > 0 && lpvChain.size() < static_cast<std::size_t>(kMaxRecoVertices);
         lpv = ph::LQ(lpv)) {
      lpvChain.push_back(lpv);
    }
    if (lpvChain.size() != recoVtx.size()) {
      throw std::runtime_error(
          "reco vertex count does not match the raw LPV chain; "
          "refusing to attach outgoing particles by ordinal");
    }
    for (std::size_t vi = 0; vi < lpvChain.size(); ++vi) {
      const int lpv = lpvChain[vi];
      auto vtx = recoVtx[vi];
      for (int lpa = ph::LQ(lpv - 1); lpa > 0; lpa = ph::LQ(lpa), ++pa_idx) {
        if (pa_idx < static_cast<int>(pa2p.size()) && pa2p[pa_idx] >= 0)
          vtx.addToParticles(tracking.particle_handles[pa2p[pa_idx]]);
      }
      if (vtx.isPrimary() && havePvHandle)
        for (const auto& p : vtx.getParticles()) pvHandle.addToParticles(p);
    }
  }

  // ----- Simulation PV (first raw simulation vertex only) -----
  // Real decay vertices live in LUJ_GenParticles; only the interaction point
  // belongs in this reconstructed-vertex collection.
  int simPv = 0;
  bool compactSimPv = false;
  std::int32_t simStatus = 0;
  if (ph::LDTOP > 0 && ph::IQ(ph::LDTOP - 2) > 28) {
    const int lpvs = ph::LQ(ph::LDTOP - 28);
    if (lpvs > 0 && ph::IQ(lpvs + 1) > 0) {
      const int npvs = ph::IQ(lpvs + 1);
      simPv = lpvs + 1 + npvs;
      compactSimPv = true;
      simStatus = (ph::IQ(simPv + 2) >> 9) & 0xf;
    }
  }
  if (simPv == 0 && ph::LDTOP > 0) simPv = ph::LQ(ph::LDTOP - 2);
  if (simPv > 0) {
    auto vtx = vtxCol.create();
    vtx.setPrimary(false);
    vtx.setAlgorithmType(kAlgoSimulation);
    fillSimulationVertex(vtx, simPv, compactSimPv);
    statusBits.push_back(simStatus);
  }

  // ----- Beam spot (direct VD package result) -----
  {
    const auto& beamSpot = event::current().beamSpot;
    auto bs = bspCol.create();
    bs.setPrimary(false);
    bs.setAlgorithmType(kAlgoBeamSpot);
    bs.setPosition({
      beamSpot.positionCm[0] * static_cast<float>(kCm2Mm),
      beamSpot.positionCm[1] * static_cast<float>(kCm2Mm),
      beamSpot.positionCm[2] * static_cast<float>(kCm2Mm),
    });
    // VDBSPT returns per-axis sigmas; we diagonalise (no off-diag info).
    bs.setCovMatrix({
      beamSpot.sigmaCm[0] * beamSpot.sigmaCm[0] * kCm2Mm2_f,  // XX
      0.f,                                                       // XY
      beamSpot.sigmaCm[1] * beamSpot.sigmaCm[1] * kCm2Mm2_f,  // YY
      0.f,                                                       // XZ
      0.f,                                                       // YZ
      beamSpot.sigmaCm[2] * beamSpot.sigmaCm[2] * kCm2Mm2_f,  // ZZ
    });
  }

  // ----- Delphi-official V0 candidates (raw LDTOP link 22) -----
  // Surface DELPHI's own V0 classification + the
  // fitted V0 quantities via Vertex.parameters so analyses can select
  // K0/Λ without re-deriving them — without these the collection is
  // just the LOOSE candidate list (S/B ~ 0.9; cutting tag→K0/Λ gives
  // the clean peaks). parameters layout:
  //   [0] tag KRV0(5): categorical — 0 loose, 1 tight, 2 K0-bkg,
  //       3 Λ-bkg, 22 K0, 33 Λ (compare by equality, not bitmask)
  //   [1] suggested V0 mass QRV0(19), SIGNED: <0 for antiparticle
  //       (e.g. Λ̄). Take std::abs() for the mass peak; the sign
  //       distinguishes Λ from Λ̄.
  //   [2] V0 momentum |p| QRV0(6) (GeV)
  //   [3] chi2 probability QRV0(7) (PXFVTX fit)
  //   [4] xy flight distance / error QRV0(11)
  //   [5] xy pointing angle to PV QRV0(12) (rad)
  if (ph::LDTOP > 0 && ph::IQ(ph::LDTOP - 2) >= 22) {
    const int bank = ph::LQ(ph::LDTOP - 22);
    if (bank > 0) {
      const int n = ph::IQ(bank + 1);
      int data = 1;
      const int version = event::current().dstVersion;
      const int pxdstVersion = ph::IQ(ph::LDTOP + 3);
      for (int i = 1; i <= n; ++i) {
        const std::int32_t header =
            static_cast<std::int32_t>(std::lround(ph::Q(bank + data + 1)));
        int recordWords = 15;
        int hypotheses = 0;
        std::int32_t tag = 0;
        if (version >= 102 || pxdstVersion >= 330) {
          recordWords = header & 0x3ff;
          hypotheses = (header >> 10) & 0xf;
          tag = (header >> 14) & 0x3f;
        } else {
          recordWords = header / 1000;
          tag = header % 1000;
        }

        auto v0 = v0Col.create();
        v0.setPrimary(false);
        v0.setAlgorithmType(kAlgoV0);
        v0.setPosition({ph::Q(bank + data + 4) * static_cast<float>(kCm2Mm),
                        ph::Q(bank + data + 5) * static_cast<float>(kCm2Mm),
                        ph::Q(bank + data + 6) * static_cast<float>(kCm2Mm)});
        v0.addToParameters(static_cast<float>(tag));
        v0.addToParameters(ph::Q(bank + data + 15));  // suggested mass
        v0.addToParameters(ph::Q(bank + data + 2));   // |p|
        v0.addToParameters(ph::Q(bank + data + 3));   // chi2 probability
        v0.addToParameters(ph::Q(bank + data + 7));   // flight/error
        v0.addToParameters(ph::Q(bank + data + 8));   // pointing angle
        add_particle_by_lpa(v0, ph::LQ(bank - 2 * i + 1));
        add_particle_by_lpa(v0, ph::LQ(bank - 2 * i));

        data += 15;
        if (version <= 102) {
          for (int h = 0; h < hypotheses; ++h) {
            const auto hypothesisHeader = static_cast<std::int32_t>(
                std::lround(ph::Q(bank + data + 1)));
            data += hypothesisHeader & 0x3ff;
          }
        } else {
          data += std::max(recordWords - 15, 0);
        }
      }
    }
  }

  // ----- Photon conversions -----
  if (event::current().dstVersion >= 103) {
    for (int parent = ph::LDTOP > 0 ? ph::LQ(ph::LDTOP - 1) : 0;
         parent > 0; parent = ph::LQ(parent)) {
      for (int photon = ph::LQ(parent - 1); photon > 0;
           photon = ph::LQ(photon)) {
        const int conversion = ph::LQ(photon - 1);
        const int code = (ph::IQ(photon + 3) >> 18) & 0x7f;
        if (conversion <= 0 || code < 21 || code > 24) continue;
        auto phc = phcCol.create();
        phc.setPrimary(false);
        phc.setAlgorithmType(kAlgoPhotonConv);
        phc.setPosition({ph::Q(conversion + 5) * static_cast<float>(kCm2Mm),
                         ph::Q(conversion + 6) * static_cast<float>(kCm2Mm),
                         ph::Q(conversion + 7) * static_cast<float>(kCm2Mm)});
        const int first = ph::LQ(conversion - 1);
        add_particle_by_lpa(phc, first);
        if (first > 0) add_particle_by_lpa(phc, ph::LQ(first));
      }
    }
  } else if (ph::LDTOP > 0 && ph::IQ(ph::LDTOP - 2) >= 24) {
    const int bank = ph::LQ(ph::LDTOP - 24);
    if (bank > 0) {
      const int n = ph::IQ(bank + 1);
      int data = 1;
      for (int i = 1; i <= n; ++i, data += 9) {
        auto phc = phcCol.create();
        phc.setPrimary(false);
        phc.setAlgorithmType(kAlgoPhotonConv);
        phc.setPosition({ph::Q(bank + data + 7) * static_cast<float>(kCm2Mm),
                         ph::Q(bank + data + 8) * static_cast<float>(kCm2Mm),
                         ph::Q(bank + data + 9) * static_cast<float>(kCm2Mm)});
        add_particle_by_lpa(phc, ph::LQ(bank - 3 * i + 2));
        add_particle_by_lpa(phc, ph::LQ(bank - 3 * i + 1));
      }
    }
  }

  // ----- Per-track impact parameters w.r.t. the primary vertex -----
  // d0PV/z0PV from SKELANA QTRAC(38,39) read 0/-999 in real DATA (the
  // common is unfilled there), so compute them geometrically from each
  // track's AtIP perigee and the PV found above. Emitted parallel to
  // sDST_TRAC_Tracks (same order), in **mm**, LCIO sign convention
  // (consistent with the TrackState the tracks carry). Linear (PV is ~mm
  // from the origin, so the helix-curvature term is negligible at this
  // precision); flag is 1 when a PV was available, 0 otherwise (value -999).
  // NOTE the dual convention: this <tag>_PV_trackD0PV is the DATA-usable one
  // (mm, LCIO, parallel to TRAC_Tracks); the Tracking.cpp <tag>_TRAC_d0PV is
  // QTRAC converted cm->mm, DELPHI sign, also parallel to TRAC_Tracks
  // (charged-only) but MC-only (QTRAC unfilled on data), so PV_trackD0PV
  // ~= -1 * TRAC_d0PV on MC (same units now, opposite sign). Do not mix them.
  podio::UserDataCollection<float>        d0pvCol;
  podio::UserDataCollection<float>        z0pvCol;
  podio::UserDataCollection<std::int32_t> d0pvFlag;
  {
    const double px = pv_x_cm * kCm2Mm, py = pv_y_cm * kCm2Mm, pz = pv_z_cm * kCm2Mm;
    const auto& tracks = frame_.get<edm4hep::TrackCollection>(
        makeName("TRAC", "Tracks"));
    for (const auto& trk : tracks) {
      float d0pv = -999.f, z0pv = -999.f; std::int32_t flag = 0;
      if (have_pv) {
        // AtIP track state (referencePoint = origin by construction).
        for (const auto& ts : trk.getTrackStates()) {
          if (ts.location != edm4hep::TrackState::AtIP) continue;
          const double phi = ts.phi;
          const double s = std::sin(phi);
          const double c = std::cos(phi);
          d0pv = static_cast<float>(ts.D0 + (px * s - py * c));
          z0pv = static_cast<float>(ts.Z0 - pz + ts.tanLambda * (px * c + py * s));
          flag = 1;
          break;
        }
      }
      d0pvCol.push_back(d0pv);
      z0pvCol.push_back(z0pv);
      d0pvFlag.push_back(flag);
    }
  }

  // ----- Push everything into the frame via the base put() -----
  put(std::move(d0pvCol),    "PV",  "Tracks_d0PV", Provenance::Custom);
  put(std::move(z0pvCol),    "PV",  "Tracks_z0PV", Provenance::Custom);
  put(std::move(d0pvFlag),   "PV",  "Tracks_ImpactFlag", Provenance::Custom);
  put(std::move(pvCol),      "PV",  "PrimaryVertex", Provenance::Transcribed);
  put(std::move(vtxCol),     "PV",  "Vertices", Provenance::Transcribed);
  put(std::move(statusBits), "PV",  "Vertices_StatusBits", Provenance::Transcribed);
  put(std::move(bspCol),     "BSP", "BeamSpot", Provenance::Derived);
  put(std::move(v0Col),      "V0",  "V0Candidates", Provenance::Transcribed);
  put(std::move(phcCol),     "PHC", "PhotonConversions", Provenance::Transcribed);
}

}  // namespace delphi_edm4hep::vertex
