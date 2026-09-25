// ParticleId domain — implementation S7 (pass-1).
//
// Walks the VECP charged-track range and emits per-track PID rows for
// dE/dx (TPC), muon, electron, and hadron-RICH. All five collections
// are kept index-aligned via `setParticle()` to the matching Particle
// handle from tracking.vecp_to_particle.
//
// Sources (skelana.txt A.3.5/7/9/10/11/12):
//   PSCDEX  : TPC dE/dx          -> sDST_HAID_dEdx + RecDqdx companion
//   PSCMUD  : muon ID            -> sDST_MUID_MuonID
//   PSCELD  : electron ID        -> sDST_ELID_ElectronID
//   PSCHAD  : combined hadron ID (KHAID + QHAID)  } merged into one
//   PSCGRC  : RICH gas    (THEG/SIGG/NPHG/NEPG)   } 18-param HadronID
//   PSCLRC  : RICH liquid (THEL/SIGL/NPHL/NEPL)   } collection

#include "delphi_edm4hep/Pid/ParticleId.h"

#include "skelana/pscdex.hpp"
#include "skelana/psceld.hpp"
#include "skelana/pscgrc.hpp"
#include "skelana/pschad.hpp"
#include "skelana/psclrc.hpp"
#include "skelana/pscmud.hpp"
#include "skelana/pscvec.hpp"   // sk::LVPART, sk::NVECP

#include "delphi_edm4hep/internal/PaWalk.h"
#include "phdst/uxcom.hpp"       // IQ
#include "phdst/uxlink.hpp"      // LDTOP

#include <edm4hep/MutableParticleID.h>
#include <edm4hep/ParticleIDCollection.h>
#include <edm4hep/Quantity.h>
#include <edm4hep/RecDqdxCollection.h>
#include <podio/Frame.h>

#include <cassert>
#include <limits>
#include <string>
#include <vector>

namespace ph = phdst;
namespace sk = skelana;

namespace delphi_edm4hep::particleid {

extern "C" {
// BBDXGET reads the MTPC module: measured dE/dx, gap size, untruncated wire
// count and method. BBDXER returns the expected error and, in IQUAL, a quality
// flag. Both live in dstana (dedxid.car).
void  bbdxget_(int* lpa, float* dedx, float* gap, int* nwir, int* nmeth);
float bbdxer_(float* dedxth, float* gap, int* nwir, int* nmeth, int* iqual);
}

namespace {

// algorithmType constants for the four pass-1 PID flavours.
constexpr std::int32_t kAlgoDedx     = 1;
constexpr std::int32_t kAlgoMuon     = 2;
constexpr std::int32_t kAlgoElectron = 3;
constexpr std::int32_t kAlgoHadronID = 4;

// PXDST version at or above which SKELANA runs BBDXGET in place of GETDEDX.
constexpr int kBbdxMinPxdst = 333;

// BBDXGET replaced GETDEDX at PXDST version 333. Both fill PSCDEX; SKELANA runs
// whichever matches the version word of the event, so the collection is named
// for the one that ran. On the BBDXGET path KDEDX(3) is the untruncated wire
// count scaled by 0.8, an estimate of the wires entering the truncated mean
// rather than a count of them.
const char* dedxAlgo() {
  const bool bbdx = ph::LDTOP > 0 && ph::IQ(ph::LDTOP + 3) >= kBbdxMinPxdst;
  return bbdx ? "BBDXGET" : "GETDEDX";
}

// dE/dx quality flag per emitted Particle: -1 no information, 0 poor .. 4
// perfect. dedxid.car:1137 advises requiring > 1.
//
// SKELANA computes this flag at skelana.car:3886 and then discards it, so it is
// recomputed here from the same inputs. The result is indexed by Particle, not
// by track, because the PA walk and the VECP loop reach the same particles
// through different orderings.
std::vector<int> dedxQuality(const tracking::Output& tracking)
{
  std::vector<int> quality(tracking.particle_handles.size(), -1);

  pawalk::forEachPA([&](int lpa, int paIdx) {
    // PAs that produced no Particle have nothing to attach a flag to.
    if (paIdx >= static_cast<int>(tracking.pa_to_particle.size())) return;
    const int p = tracking.pa_to_particle[paIdx];
    if (p < 0) return;

    // Measured dE/dx, gap size, untruncated wire count and method, all read
    // from the MTPC module of this PA.
    float dedx = -1.f, gap = 0.f;
    int   nwir = 0, nmeth = 0;
    bbdxget_(&lpa, &dedx, &gap, &nwir, &nmeth);

    // BBDXGET reports -1 when the track has no dE/dx measurement.
    if (dedx <= 0.f || nwir < 1) return;

    // The return value is the expected error, which SKELANA already stores as
    // QDEDX(5); the flag returned alongside it is what is taken here.
    int iqual = -1;
    bbdxer_(&dedx, &gap, &nwir, &nmeth, &iqual);
    quality[p] = iqual;
  });

  return quality;
}

}  // namespace

void ParticleIdWriter::emit()
{
  edm4hep::ParticleIDCollection dedxCol;
  edm4hep::RecDqdxCollection    dqdxCol;
  edm4hep::ParticleIDCollection muonCol;
  edm4hep::ParticleIDCollection elecCol;
  edm4hep::ParticleIDCollection hadrCol;

  if (!ctx_.tracking) {
    // No tracks emitted upstream — still emit empty collections so
    // the schema remains stable across events.
    put(std::move(dedxCol), dedxAlgo(), "Dedx", Provenance::Derived);
    put(std::move(dqdxCol), dedxAlgo(), "DedxRecDqdx", Provenance::Derived);
    put(std::move(muonCol), "MUID", "MuonID", Provenance::Transcribed);
    put(std::move(elecCol), "ELID", "ElectronID", Provenance::Transcribed);
    put(std::move(hadrCol), "HAID", "HadronID", Provenance::Transcribed);
    return;
  }
  const auto& tracking = *ctx_.tracking;
  const std::vector<int> quality = dedxQuality(tracking);

  // Helper: create a per-particle PID row of the given algorithmType,
  // attach to the matching Particle, and return the row for parameter
  // population by the caller.
  auto makePid = [&](edm4hep::ParticleIDCollection& coll,
                     int vecp_i,
                     std::int32_t algoType)
    -> edm4hep::MutableParticleID
  {
    auto pid = coll.create();
    pid.setAlgorithmType(algoType);
    const int p_idx = tracking.vecp_to_particle[vecp_i];
    pid.setParticle(tracking.particle_handles[p_idx]);
    return pid;
  };

  // PSCVEC with IFLODR=1 orders charged particles at indices
  // LVPART..NCVECP (= 1..NCVECP since LVPART=1), neutrals after.
  // All pass-1 PID collections (dE/dx, MUID, ELID, HadronID) are
  // **charged-only** per the SKELANA documentation — neutrals have
  // no track in the TPC / muon chamber / RICH so their PSCDEX /
  // PSCMUD / PSCELD / PSCHAD entries are all-zero. We bound the loop
  // to NCVECP to keep the emitted collection size = charged-track count.
  const int last_charged = sk::NCVECP;   // 1-based inclusive
  for (int i = sk::LVPART; i <= last_charged; ++i) {
    if (i >= static_cast<int>(tracking.vecp_to_particle.size())) break;
    if (tracking.vecp_to_particle[i] < 0) continue;

    // ---- TPC dE/dx (PSCDEX) ----
    //   QDEDX(1) = dE/dx value (MIP-normalised)
    //   QDEDX(5) = error on the dE/dx measurement (sigma; NOT Landau width)
    //   KDEDX(3) = # wires used
    // Per-track emit policy: emit when there's real data (dE/dx > 0 or
    // wires > 0) — distinguishes "PSCDEX didn't run for this track"
    // from "negative-tag result".
    {
      const float dedx    = sk::QDEDX(1, i);
      const float sigma   = sk::QDEDX(5, i);
      const int   nWires  = sk::KDEDX(3, i);
      if (dedx > 0.f || nWires > 0) {
        auto pid = makePid(dedxCol, i, kAlgoDedx);
        pid.addToParameters(dedx);
        pid.addToParameters(sigma);
        pid.addToParameters(
            static_cast<float>(quality[tracking.vecp_to_particle[i]]));

        // RecDqdx parallel form (FCC-style consumers consume the typed
        // Quantity rather than ParticleID parameters[]). Type=1 marks
        // "TPC truncated-mean dE/dx".
        auto dq = dqdxCol.create();
        edm4hep::Quantity q;
        q.type  = 1;
        q.value = dedx;
        q.error = sigma;
        dq.setDQdx(q);
        // Link the RecDqdx to its Track via the charged Particle's
        // Particle->Track relation (set in tracking; one track per charged
        // PFO). PidHybrid re-points the clone onto the fDST track in pass 2.
        const auto dq_part =
            tracking.particle_handles[tracking.vecp_to_particle[i]];
        for (const auto& trk : dq_part.getTracks()) { dq.setTrack(trk); break; }
      }
    }

    // ---- Muon ID (PSCMUD) ----
    // Slot layout and the tag bit masks are in Pid/ParticleId.h.
    {
      const int tag = sk::KMUID(1, i);
      if (tag != 0) {
        auto pid = makePid(muonCol, i, kAlgoMuon);
        pid.addToParameters(static_cast<float>(tag));
        pid.addToParameters(sk::QMUID(2, i));
        pid.addToParameters(static_cast<float>(sk::KMUID(3, i)));
        assert(pid.getParameters().size() == kMuCount);
      }
    }

    // ---- Electron ID (PSCELD) ----
    //   KELID(1) = ELECID tag (0..5):
    //              0 = no ID run; 1 = NOT electron;
    //              2 = very loose; 3 = loose; 4 = standard; 5 = tight
    //   KELID(2) = gamma-conversion tag (1..3)
    {
      const int tag = sk::KELID(1, i);
      if (tag != 0) {
        auto pid = makePid(elecCol, i, kAlgoElectron);
        pid.addToParameters(static_cast<float>(tag));
        pid.addToParameters(static_cast<float>(sk::KELID(2, i)));
      }
    }

    // ---- Hadron-RICH combined (PSCHAD + PSCGRC + PSCLRC) ----
    // 18-element parameters in fixed order. Always emitted for any
    // VECP-matched track so the collection length is parallel-ish to
    // Particles for charged tracks (downstream consumers can index by
    // setParticle() rather than position).
    //
    // Params [12] (gasFlag, FLAGG) and [17] (liqFlag, FLAGL) carry the
    // RICH RING/VETO quality word. These are Fortran REALs whose value
    // is the (small) integer flag (SKELANA does FLAGG(I)=KGRIC(5,I)),
    // so they are emitted verbatim; read downstream via int(round(v)).
    {
      auto pid = makePid(hadrCol, i, kAlgoHadronID);
      // PSCHAD KHAID / QHAID
      pid.addToParameters(static_cast<float>(sk::KHAID(4, i)));   // kaonRich
      pid.addToParameters(static_cast<float>(sk::KHAID(5, i)));   // protonRich
      pid.addToParameters(static_cast<float>(sk::KHAID(6, i)));   // pionRich
      pid.addToParameters(static_cast<float>(sk::KHAID(2, i)));   // kaonDedx
      pid.addToParameters(static_cast<float>(sk::KHAID(3, i)));   // protonDedx
      pid.addToParameters(sk::QHAID(7, i));                       // xKaon
      pid.addToParameters(sk::QHAID(8, i));                       // xProton
      pid.addToParameters(static_cast<float>(sk::KHAID(9, i)));   // richQuality
      // PSCGRC (RICH gas)
      pid.addToParameters(sk::THEG(i));                            // θ_C gas (rad)
      pid.addToParameters(sk::SIGG(i));                            // σ_θ gas
      pid.addToParameters(static_cast<float>(sk::NPHG(i)));        // N_ph gas
      pid.addToParameters(static_cast<float>(sk::NEPG(i)));        // N_expected gas
      // FLAGG = KGRIC(5,I): RICH-gas RING/VETO quality flag, a small
      // integer carried in a Fortran REAL slot. Emit verbatim; read
      // downstream via int(round(v)). Observed values {0,1,7,9,15,25,
      // 31,39,47,63}. Bit semantics per the DELPHI HAID-module doc
      // (longdes §3.04, "+4 quality flag"); consult it before cutting.
      pid.addToParameters(sk::FLAGG(i));                           // FLAGG quality word
      // PSCLRC (RICH liquid)
      pid.addToParameters(sk::THEL(i));
      pid.addToParameters(sk::SIGL(i));
      pid.addToParameters(static_cast<float>(sk::NPHL(i)));
      pid.addToParameters(static_cast<float>(sk::NEPL(i)));        // N_expected liquid
      pid.addToParameters(sk::FLAGL(i));                           // FLAGL quality word
    }
  }

  put(std::move(dedxCol), dedxAlgo(), "Dedx", Provenance::Derived);
  put(std::move(dqdxCol), dedxAlgo(), "DedxRecDqdx", Provenance::Derived);
  put(std::move(muonCol), "MUID", "MuonID", Provenance::Transcribed);
  put(std::move(elecCol), "ELID", "ElectronID", Provenance::Transcribed);
  put(std::move(hadrCol), "HAID", "HadronID", Provenance::Transcribed);
}

// Pass-2 PID (TOF, MTPC-extended, TE FitQuality companion) will live in
// a separate writer added during step-2 wiring.

}  // namespace delphi_edm4hep::particleid
