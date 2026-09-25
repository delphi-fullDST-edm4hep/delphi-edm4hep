// ParticleId domain: per-track PID + dE/dx + RICH from SKELANA commons.
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

#include <cstddef>
#include <cstdint>

namespace delphi_edm4hep::particleid {

/// Slot layout of the `MUID_MuonID` `parameters` VectorMember. EDM4hep types it
/// as a bare float vector, so slot meanings are convention: declared here,
/// appended in this order by ParticleId.cpp.
/// @collection{MUID_MuonID}
enum MuonIdIndex : std::size_t {
  kMuTag = 0,   ///< `KMUID(1)` MUCAL2 tag; a bit mask, see #MuonIdTag
  kMuChi2,      ///< `QMUID(2)` global chi2 of the very-loose refit
  kMuHits,      ///< `KMUID(3)` hit pattern with inefficiencies
  kMuCount
};

/// Bits of #kMuTag, as masks. DELPHI numbers these bits from 1.
///
/// The four chamber tags nest: tight implies standard implies loose implies
/// very loose. #kMuHcal is independent and occurs alone, so `tag >= 2` is not
/// "loose or better" -- it also admits an HCAL-only tag carrying no
/// muon-chamber requirement. Set by `MUFLAG`'s `RTAG` on the chamber refit.
/// @bits{MUID_MuonID,kMuTag}
enum MuonIdTag : std::int32_t {
  kMuVeryLoose = 1 << 0,  ///< bit 1; chi2 cuts only
  kMuLoose     = 1 << 1,  ///< bit 2; no hit-pattern requirement
  kMuStandard  = 1 << 2,  ///< bit 3; loosest tag requiring a hit outside the iron
  kMuTight     = 1 << 3,  ///< bit 4; tightest chi2 cuts, a different refit
  kMuHcal      = 1 << 4   ///< HCAL, no muon-chamber requirement
};

class ParticleIdWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;
  void emit() override;
};

}  // namespace delphi_edm4hep::particleid
