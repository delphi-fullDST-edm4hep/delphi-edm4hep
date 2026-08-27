// Btag.h — b-tagging domain writer.
//
// Emits DELPHI b-tagging output. What is available depends on the job's
// BtagMode (see BtagMode.h):
//
//   Off     source-local BTAGCFG provenance only; no b-tag payload.
//   Bank    on a shortDST, the PSCBTG event/hemisphere probabilities read
//           from the official stored BTAG bank and emitted under BTG; on a
//           fullDST, SKELANA recalculates and output is emitted under AABTAG.
//   Recalc  the same probabilities, recalculated by AABTAG, PLUS the
//           per-track quantities from the AAMAIN / AAMNVX commons and
//           AABTAG's own primary vertex. Emitted under AABTAG.
//
// The BTG / AABTAG split is deliberate and is the point of the naming:
// BTG is a bank transcription, AABTAG is a rerun of a reconstruction
// algorithm at conversion time. They are not interchangeable and must
// not share a collection name. AABTAG is not a bank mnemonic, which is
// exactly the signal -- a reader who sees it should go and find out why.
//
// Per-track arrays are emitted in AABTAG's OWN track ordering (1..NTRK),
// not remapped onto the Track/Particle collections, with a parallel
// _ParticleIndex giving the <source>_MAIN_Particles entry for each (-1
// when unresolvable). That mirrors sDST_ELTR_ParticleIndex and keeps the
// AABTAG selection (ISRT) meaningful; a remap would have to invent
// entries for tracks AABTAG never considered.

#pragma once

#include "delphi_edm4hep/BtagMode.h"
#include "delphi_edm4hep/CollectionWriter.h"

namespace delphi_edm4hep::btag {

class BtagWriter : public CollectionWriter {
public:
  // `fulldst` selects the SKELANA dispatch branch this pass runs under
  // (pass 2 drives on a fullDST, where Bank degrades to a recalculation).
  BtagWriter(podio::Frame& frame,
             EventContext& ctx,
             std::string_view source_tag,
             BtagMode mode,
             bool fulldst)
    : CollectionWriter(frame, ctx, source_tag), mode_(mode), fulldst_(fulldst) {}

  void emit() override;

private:
  // True when this pass actually ran AABTAG (as opposed to reading the
  // stored bank), i.e. when the AAMAIN / AAMNVX commons are populated.
  bool recalculated() const {
    return mode_ == BtagMode::Recalc || (mode_ == BtagMode::Bank && fulldst_);
  }
  // Collection-name mnemonic implied by the above.
  std::string_view mnemonic() const { return recalculated() ? "AABTAG" : "BTG"; }

  BtagMode mode_;
  bool     fulldst_;
};

}  // namespace delphi_edm4hep::btag
