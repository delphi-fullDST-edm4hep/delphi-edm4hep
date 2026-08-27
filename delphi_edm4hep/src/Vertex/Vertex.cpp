// Vertex domain — implementation S4.
//
// Reads PSCVTX (reco PV chain + simulation PV chain), PSCBSP (beam spot),
// PSCRV0 (Delphi-official V0), PSCPHC (photon conversions). All positions
// converted from DELPHI cm to EDM4hep mm. Vertex-to-Particle relations are
// collection-specific: the reco PV chain carries DELPHI's outgoing PA
// assignment, while V0 and photon-conversion vertices carry their fitted
// daughters.

#include "delphi_edm4hep/Vertex/Vertex.h"

#include "skelana/pscbsp.hpp"
#include "skelana/pscphc.hpp"
#include "skelana/pscrv0.hpp"
#include "skelana/pscvtx.hpp"

#include "phdst/uxcom.hpp"   // ph::LQ, ph::IQ, ph::Q  (raw ZEBRA store)
#include "phdst/uxlink.hpp"  // ph::LDTOP

#include <edm4hep/MutableVertex.h>
#include <edm4hep/VertexCollection.h>
#include <edm4hep/TrackCollection.h>
#include <edm4hep/TrackState.h>
#include <podio/Frame.h>
#include <podio/UserDataCollection.h>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace sk = skelana;
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

// DELPHI PSCVTX/LPV status bits (KVTX(17,j) / IQ(lpv)). Bit 1 marks a
// dummy vertex and bit 2 a secondary vertex. A dummy first slot is the
// unfitted beam-spot bucket and must never be published as the event PV.
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

// Fill the geometric content of a Vertex from PSCVTX entry j. Pulls
// position (cm->mm), covariance (cm^2 -> mm^2), chi^2, and ndf.
void fillFromPSCVTX(edm4hep::MutableVertex vtx, int j) {
  vtx.setPosition({
    static_cast<float>(sk::QVTX(6, j) * kCm2Mm),
    static_cast<float>(sk::QVTX(7, j) * kCm2Mm),
    static_cast<float>(sk::QVTX(8, j) * kCm2Mm),
  });
  vtx.setChi2(sk::QVTX(9, j));
  vtx.setNdf (sk::KVTX(4, j));
  vtx.setCovMatrix({
    sk::QVTX(10, j) * kCm2Mm2_f,   // XX
    sk::QVTX(11, j) * kCm2Mm2_f,   // XY
    sk::QVTX(12, j) * kCm2Mm2_f,   // YY
    sk::QVTX(13, j) * kCm2Mm2_f,   // XZ
    sk::QVTX(14, j) * kCm2Mm2_f,   // YZ
    sk::QVTX(15, j) * kCm2Mm2_f,   // ZZ
  });
}

// Fill a Vertex from the raw ZEBRA vertex bank at offset lpv (the
// LQ(LDTOP-1) chain). This is the DELANA reco PV chain used whenever the
// SKELANA PSCVTX primary slot is unavailable. Layout mirrors
// delphi-raw-nanoaod's fillVtx: Q(lpv+5..7) = position cm,
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

// Does PSCVTX carry a geometrically usable first slot? SKELANA can populate
// this common for both data and MC; the -999 cm sentinel selects the raw
// ZEBRA-chain fallback. Dummy status is handled separately below.
bool pscvtxPrimaryUsable() {
  return sk::NVTX >= 1 && sk::QVTX(6, 1) > -990.0f;
}

}  // namespace

void VertexWriter::emit()
{
  // Need tracking output for V0 / PhotonConv -> Particle relations.
  if (!ctx_.tracking) {
    // Still emit empty collections so downstream schema is stable.
    put(edm4hep::VertexCollection{},           "PV",  "PrimaryVertex");
    put(edm4hep::VertexCollection{},           "PV",  "Vertices");
    put(podio::UserDataCollection<std::int32_t>{}, "PV",  "Vertices_StatusBits");
    put(edm4hep::VertexCollection{},           "BSP", "BeamSpot");
    put(edm4hep::VertexCollection{},           "V0",  "V0Candidates");
    put(edm4hep::VertexCollection{},           "PHC", "PhotonConversions");
    return;
  }
  const auto& tracking = *ctx_.tracking;

  edm4hep::VertexCollection             pvCol;
  edm4hep::VertexCollection             vtxCol;
  podio::UserDataCollection<std::int32_t> statusBits;
  edm4hep::VertexCollection             bspCol;
  edm4hep::VertexCollection             v0Col;
  edm4hep::VertexCollection             phcCol;

  // Helper: attach a VECP-indexed Particle to a Vertex via addToParticles.
  // Silently no-ops if the VECP entry is out of range or has no matching
  // emitted Particle.
  auto add_particle_by_vecp = [&](edm4hep::MutableVertex vtx, int vecp_idx) {
    const auto& map = tracking.vecp_to_particle;
    if (vecp_idx < 1 || vecp_idx >= static_cast<int>(map.size())) return;
    const int p_idx = map[vecp_idx];
    if (p_idx < 0) return;
    vtx.addToParticles(tracking.particle_handles[p_idx]);
  };

  // Reco vertices captured in creation order (== LPV-chain order on both the
  // PSCVTX and LDTOP paths) + a handle to the standalone PrimaryVertex copy,
  // so the per-vertex outgoing-particle assignments can be wired in after
  // creation.
  std::vector<edm4hep::MutableVertex> recoVtx;
  edm4hep::MutableVertex pvHandle{};
  bool havePvHandle = false;

  // ----- Reconstructed PV chain (PSCVTX entries 1..NVTX) -----
  // Convention: entry j=1 is the primary vertex unless status bit 1 (dummy)
  // or bit 2 (secondary) is set. The full chain, including dummy entries, is
  // also emitted into `Vertices` with the raw per-entry status bits.
  //
  // PXTAG D⁰/D±/Dₛ-candidate bits (5+) in IQ(LPV) per dst_content.txt
  // p.3 are preserved verbatim in the `Vertices_StatusBits` UserData
  // — no semantic interpretation done here. Consumers needing the
  // per-vertex flavour-tag flags read them off the raw int32.
  // Primary-vertex position in DELPHI cm, captured for the d0PV/z0PV
  // impact computation below. Stays sentinel if no usable PV is found.
  double pv_x_cm = -999.0, pv_y_cm = -999.0, pv_z_cm = -999.0;
  bool   have_pv = false;

  if (pscvtxPrimaryUsable()) {
    // Use the SKELANA common when its first slot is geometrically available.
    for (int j = 1; j <= sk::NVTX; ++j) {
      const std::int32_t status = sk::KVTX(17, j);
      const bool is_primary = isPrimaryRecoVertex(j == 1, status);

      auto vtx = vtxCol.create();
      vtx.setPrimary(is_primary);
      vtx.setAlgorithmType(is_primary ? kAlgoPrimary : kAlgoNonPrimaryReco);
      fillFromPSCVTX(vtx, j);
      statusBits.push_back(status);
      recoVtx.push_back(vtx);

      if (is_primary) {
        auto pv = pvCol.create();
        pv.setPrimary(true);
        pv.setAlgorithmType(kAlgoPrimary);
        fillFromPSCVTX(pv, j);
        pv_x_cm = sk::QVTX(6, j); pv_y_cm = sk::QVTX(7, j); pv_z_cm = sk::QVTX(8, j);
        have_pv = true;
        pvHandle = pv; havePvHandle = true;
      }
    }
  } else {
    // PSCVTX is unavailable: read the DELANA reco vertex chain straight from
    // the raw ZEBRA store at LQ(LDTOP-1).
    int lpv = (ph::LDTOP > 0) ? ph::LQ(ph::LDTOP - 1) : 0;
    int count = 0;
    while (lpv > 0 && count < kMaxRecoVertices) {
      const std::int32_t status = ph::IQ(lpv);
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
        pv_x_cm = ph::Q(lpv + 5); pv_y_cm = ph::Q(lpv + 6); pv_z_cm = ph::Q(lpv + 7);
        have_pv = true;
        pvHandle = pv; havePvHandle = true;
      }
      ++count;
      lpv = ph::LQ(lpv);
    }
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
          "PSCVTX/reco vertex count does not match the raw LPV chain; "
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

  // ----- Simulation PV (PSCVTX sim region: first entry only) -----
  // PSCVTX stores reco at 1..NVTXMX and simulation at NVTXMX+1..2*NVTXMX.
  // Only the FIRST sim slot (NVTXMX+1) is reliable: it is the true IP
  // (verified -- matches the MCParticle primary in 158/160 mumu/qq/bb
  // events). NVTXMC over-counts on our DELSIM SDST, so slots 2..NVTXMC read
  // uninitialised memory (monotonically growing, non-physical positions with
  // no MCParticle counterpart) -- we must NOT emit those. Real simulation
  // DECAY vertices live in sDST_LUJ_GenParticles (the reliable truth record).
  if (sk::NVTXMC >= 1) {
    const int j = sk::NVTXMX + 1;
    auto vtx = vtxCol.create();
    vtx.setPrimary(false);
    vtx.setAlgorithmType(kAlgoSimulation);
    fillFromPSCVTX(vtx, j);
    statusBits.push_back(sk::KVTX(17, j));
  }

  // ----- Beam spot (PSCBSP) -----
  {
    auto bs = bspCol.create();
    bs.setPrimary(false);
    bs.setAlgorithmType(kAlgoBeamSpot);
    bs.setPosition({
      sk::XYZBS(1) * static_cast<float>(kCm2Mm),
      sk::XYZBS(2) * static_cast<float>(kCm2Mm),
      sk::XYZBS(3) * static_cast<float>(kCm2Mm),
    });
    // PSCBSP stores per-axis sigmas; we diagonalise (no off-diag info).
    bs.setCovMatrix({
      sk::DXYZBS(1) * sk::DXYZBS(1) * kCm2Mm2_f,   // XX
      0.f,                                          // XY
      sk::DXYZBS(2) * sk::DXYZBS(2) * kCm2Mm2_f,   // YY
      0.f,                                          // XZ
      0.f,                                          // YZ
      sk::DXYZBS(3) * sk::DXYZBS(3) * kCm2Mm2_f,   // ZZ
    });
  }

  // ----- Delphi-official V0 candidates (PSCRV0) -----
  // QRV0(8..10) = vertex (X, Y, Z) in cm; KRV0(1..2) = VECP indices of
  // the two daughters. Surface DELPHI's own V0 classification + the
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
  for (int i = 1; i <= sk::NRV0; ++i) {
    auto v0 = v0Col.create();
    v0.setPrimary(false);
    v0.setAlgorithmType(kAlgoV0);
    v0.setPosition({
      sk::QRV0(8,  i) * static_cast<float>(kCm2Mm),
      sk::QRV0(9,  i) * static_cast<float>(kCm2Mm),
      sk::QRV0(10, i) * static_cast<float>(kCm2Mm),
    });
    v0.addToParameters(static_cast<float>(sk::KRV0(5, i)));  // tag
    v0.addToParameters(sk::QRV0(19, i));                     // suggested mass
    v0.addToParameters(sk::QRV0(6,  i));                     // |p|
    v0.addToParameters(sk::QRV0(7,  i));                     // chi2 prob
    v0.addToParameters(sk::QRV0(11, i));                     // flight/err
    v0.addToParameters(sk::QRV0(12, i));                     // pointing angle
    // PSCRV0 doesn't carry a per-V0 cov matrix in the same docs-aligned
    // slots that PSCVTX does (QRV0(24..33) is the V0 weight matrix in a
    // different basis — flagged for follow-up).
    add_particle_by_vecp(v0, sk::KRV0(1, i));
    add_particle_by_vecp(v0, sk::KRV0(2, i));
  }

  // ----- Photon conversions (PSCPHC) -----
  // QPHOC(10..12) = conversion (X, Y, Z) in cm; KPHOC(1..2) = VECP
  // indices of the e+/e- daughters.
  for (int i = 1; i <= sk::NPHOC; ++i) {
    auto phc = phcCol.create();
    phc.setPrimary(false);
    phc.setAlgorithmType(kAlgoPhotonConv);
    phc.setPosition({
      sk::QPHOC(10, i) * static_cast<float>(kCm2Mm),
      sk::QPHOC(11, i) * static_cast<float>(kCm2Mm),
      sk::QPHOC(12, i) * static_cast<float>(kCm2Mm),
    });
    add_particle_by_vecp(phc, sk::KPHOC(1, i));
    add_particle_by_vecp(phc, sk::KPHOC(2, i));
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
        std::string(source_tag_) + "_TRAC_Tracks");
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
  put(std::move(d0pvCol),    "PV",  "trackD0PV");
  put(std::move(z0pvCol),    "PV",  "trackZ0PV");
  put(std::move(d0pvFlag),   "PV",  "trackImpactFlag");
  put(std::move(pvCol),      "PV",  "PrimaryVertex");
  put(std::move(vtxCol),     "PV",  "Vertices");
  put(std::move(statusBits), "PV",  "Vertices_StatusBits");
  put(std::move(bspCol),     "BSP", "BeamSpot");
  put(std::move(v0Col),      "V0",  "V0Candidates");
  put(std::move(phcCol),     "PHC", "PhotonConversions");
}

}  // namespace delphi_edm4hep::vertex
