// MtpcWriter — implementation. Runs in both passes.
//
// PA.MTPC v1.04+ layout (shortdes.txt MTPC p.41 / dst_content.txt p.26):
//   Q(LMTPC+1)   label (= 7)
//   Q(LMTPC+2)   dE/dx 80% truncated mean (MIP-normalised)
//   Q(LMTPC+3)   sigma of the 80% truncated Landau
//   Q(LMTPC+4)   dE/dx 65% truncated mean
//   Q(LMTPC+5)   sigma 65%
//   Q(LMTPC+6)   dE/dx integrated 80%
//   Q(LMTPC+7)   npads + 100*sec_start + 10000*sec_end
//   Q(LMTPC+8)   nwires + 1000*nhitwires + 1000000*method
//   Q(LMTPC+10)  packed bit field: saturated hits (bits 1..8), empty wires
//                 (9..16), last wire/sector (17..20), and an internal
//                 generated-random marker (bit 21)
//   Q(LMTPC+15)  V0 pad-row pattern (16+4 bits)

#include "delphi_edm4hep/Pid/Mtpc.h"

#include "delphi_edm4hep/internal/PaWalk.h"

#include <edm4hep/MutableParticleID.h>
#include <edm4hep/MutableRecDqdx.h>
#include <edm4hep/ParticleIDCollection.h>
#include <edm4hep/Quantity.h>
#include <edm4hep/RecDqdxCollection.h>
#include <edm4hep/ReconstructedParticleCollection.h>

#include <cmath>
#include <cstdint>

namespace ph = phdst;

namespace delphi_edm4hep::mtpc {

namespace {
constexpr std::int32_t kAlgoMtpcExt = 6;
}

void MtpcWriter::emit()
{
  edm4hep::ParticleIDCollection pid_col;
  edm4hep::RecDqdxCollection    dqdx_col;

  pawalk::forEachPA([&](int lpa, int paIdx) {
    const int lmtpc = pawalk::lphpa("MTPC", lpa);
    if (lmtpc <= 0) return;
    const auto particle = particleForPa(paIdx);
    if (!particle) return;

    const float dedx80   = ph::Q(lmtpc + 2);
    const float sigma80  = ph::Q(lmtpc + 3);
    const float dedx65   = ph::Q(lmtpc + 4);
    const float sigma65  = ph::Q(lmtpc + 5);
    const float dedx_int = ph::Q(lmtpc + 6);
    const int   p7       = static_cast<int>(std::lround(ph::Q(lmtpc + 7)));
    const int   p8       = static_cast<int>(std::lround(ph::Q(lmtpc + 8)));
    const int   p10      = static_cast<int>(std::lround(ph::Q(lmtpc + 10)));
    const int   padpat   = static_cast<int>(std::lround(ph::Q(lmtpc + 15)));
    const int   nPads    = p7  % 100;
    const int   nWires   = p8  % 1000;
    // Decode the documented low-byte fields instead of decimal splitting.
    // GETNORM sets bit 21 in place; masking by construction keeps converter
    // output independent of whether a legacy calibration ran earlier.
    const int   nSat     = p10 & 0xff;
    const int   nEmpty   = (p10 >> 8) & 0xff;

    auto pid = pid_col.create();
    pid.setAlgorithmType(kAlgoMtpcExt);
    pid.addToParameters(dedx80);
    pid.addToParameters(sigma80);
    pid.addToParameters(dedx65);
    pid.addToParameters(sigma65);
    pid.addToParameters(dedx_int);
    pid.addToParameters(static_cast<float>(nPads));
    pid.addToParameters(static_cast<float>(nWires));
    pid.addToParameters(static_cast<float>(nSat));
    pid.addToParameters(static_cast<float>(nEmpty));
    pid.addToParameters(static_cast<float>(padpat));
    pid.setParticle(*particle);

    // RecDqdx parallel: type=1 = TPC truncated mean.
    auto dq = dqdx_col.create();
    edm4hep::Quantity q;
    q.type  = 1;
    q.value = dedx80;
    q.error = sigma80;
    dq.setDQdx(q);
    // Link to the track via the fDST Particle's Particle->Track relation
    // (set by MainHybrid, which runs before this writer). One track per
    // charged MTPC PA.
    for (const auto& trk : particle->getTracks()) { dq.setTrack(trk); break; }
  });

  put(std::move(pid_col),  "MTPC", "dEdxExtended", Provenance::Transcribed);
  put(std::move(dqdx_col), "MTPC", "dEdx_RecDqdx", Provenance::Transcribed);
}

}  // namespace delphi_edm4hep::mtpc
