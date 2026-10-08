// TblHybridWriter — pass-2 implementation.

#include "delphi_edm4hep/Truth/TblHybrid.h"

#include <edm4hep/MCParticleCollection.h>
#include <edm4hep/RecoMCParticleLinkCollection.h>
#include <edm4hep/ReconstructedParticleCollection.h>

#include <cstddef>

namespace delphi_edm4hep::tbl_hybrid {

void TblHybridWriter::emit()
{
  edm4hep::RecoMCParticleLinkCollection out;

  const auto& src =
    frame_.get<edm4hep::RecoMCParticleLinkCollection>(sdstName("TBL", "RecoToMC"));
  const auto& fdst_particles =
    frame_.get<edm4hep::ReconstructedParticleCollection>("fDST_MAIN_Particles");
  const std::size_t n_fdst = fdst_particles.size();

  for (const auto& sl : src) {
    auto dl = out.create();
    dl.setWeight(sl.getWeight());
    const auto& src_from = sl.getFrom();
    const auto  fidx     = src_from.getObjectID().index;
    if (fidx >= 0 && static_cast<std::size_t>(fidx) < n_fdst) {
      dl.setFrom(fdst_particles[fidx]);
    }
    // `to` (MCParticle) unchanged — sDST_STSH_MCParticles is preserved
    // as-is in the fDST view (no MC re-emission), so the original
    // reference is still valid.
    dl.setTo(sl.getTo());
  }

  put(std::move(out), "TBL", "RecoToMC", Provenance::Transcribed);
}

}  // namespace delphi_edm4hep::tbl_hybrid
