// ParticleId domain: direct per-PA PID, dE/dx, and RICH decoding.
// ParticleIdWriter (pass-1) reads ctx_.tracking and emits:
//   <tag>_HAID_dEdx                ParticleIDCollection (algoType=1)
//   <tag>_HAID_dEdx_RecDqdx        RecDqdxCollection
//   <tag>_MUID_MuonID              ParticleIDCollection (algoType=2)
//   <tag>_ELID_ElectronID          ParticleIDCollection (algoType=3)
//   <tag>_HAID_HadronID            ParticleIDCollection (algoType=4; 18 params)
//
// Pass-2 (TOF / MTPC-extended / TE FitQuality companion) will be a
// separate writer class, added in step 2.

#pragma once

#include "delphi_edm4hep/CollectionWriter.h"

namespace delphi_edm4hep::particleid {

class ParticleIdWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;
  void emit() override;
};

}  // namespace delphi_edm4hep::particleid
