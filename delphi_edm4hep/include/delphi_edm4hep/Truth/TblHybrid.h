// TblHybrid domain — pass-2 hybrid cascade writer.
//
// Re-emits sDST_TBL_RecoToMC → fDST_TBL_RecoToMC with the `from`
// relation re-pointed at fDST_MAIN_Particles. The `to` (MCParticle)
// stays pointing at sDST_STSH_MCParticles which is unchanged.

#pragma once

#include "delphi_edm4hep/CollectionWriter.h"

namespace delphi_edm4hep::tbl_hybrid {

class TblHybridWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;
  void emit() override;
};

}  // namespace delphi_edm4hep::tbl_hybrid
