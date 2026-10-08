// Truth domain — split into two writers.
//
// TruthGenWriter      : unpacks PSCLUJ (via PSHLUJ/PSFLUJ gating) and the
//                       simulated tracks, emits <tag>_STSH_MCParticles
//                       (generator event + particles made by DELSIM), sets
//                       ctx_.gen_truth.
// TruthRecoLinkWriter : reads ctx_.gen_truth + ctx_.tracking, walks
//                       PSCTBL (IPAST then ISTLU) exact tables, emits
//                       <tag>_TBL_RecoToMC (RecoMCParticleLink).
//
// The two-writer split is needed because the link emission depends on
// Tracking having already run (so ctx_.tracking is populated).

#pragma once

#include "delphi_edm4hep/CollectionWriter.h"
#include "delphi_edm4hep/Truth/TruthData.h"   // GenParticleResult

#include <cstdint>

namespace delphi_edm4hep::truth {

/// What `generatorStatus` means on `STSH_MCParticles`.
///
/// Generator lines carry JETSET's status code KS, which DELSIM stores as
/// K(I,1) = 10000 KS + KH (STSH bank) and SKELANA divides back out; the
/// meanings follow the JETSET/PYTHIA 6 event-record convention. Particles the
/// detector simulation created have status 0.
///
/// @collection{STSH_MCParticles}
enum GeneratorStatus : std::int32_t {
  kStatusSimulated        = 0,   ///< created by DELSIM, with
                                 ///< `isCreatedInSimulation` set: decay products
                                 ///< of the particles JETSET left undecayed
                                 ///< (K0S, Lambda, Sigma, Xi, Omega, and pi, K, mu
                                 ///< decaying in flight), and secondaries of
                                 ///< interactions in the detector (photon
                                 ///< conversions, bremsstrahlung, hadronic
                                 ///< interactions). The parent is the particle
                                 ///< entering its origin vertex; PDG is
                                 ///< translated from DELPHI's particle code, with
                                 ///< nuclei as nuclear codes
  kStatusUndecayed        = 1,   ///< JETSET: undecayed particle. DELSIM may still
                                 ///< have decayed it or ended it in an
                                 ///< interaction; its endpoint is then that
                                 ///< vertex and its daughters have status 0
  kStatusDecayed          = 11,  ///< JETSET: decayed particle, or a fragmented
                                 ///< parton that is the last of its colour
                                 ///< singlet
  kStatusFragmentedParton = 12,  ///< JETSET: fragmented parton followed by more
                                 ///< partons of the same colour singlet; also a
                                 ///< B meson that decayed after mixing
  kStatusRearrangedParton = 13,  ///< JETSET: parton removed when special colour
                                 ///< flow rearranged its parton system
  kStatusBranchedParton   = 14   ///< JETSET: parton that branched into further
                                 ///< partons, with special colour flow
};

class TruthGenWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;
  void emit() override;
};

class TruthRecoLinkWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;
  void emit() override;
};

}  // namespace delphi_edm4hep::truth
