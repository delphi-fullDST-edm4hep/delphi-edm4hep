// Truth domain — split into two writers.
//
// TruthGenWriter      : directly decodes compact/full simulation banks,
//                       emits <tag>_LUJ_GenParticles, sets
//                       ctx_.gen_truth.handles.
// TruthRecoLinkWriter : reads ctx_.gen_truth + ctx_.tracking, walks
//                       the direct PA -> simulated-particle map, emits
//                       <tag>_TBL_RecoToGen (RecoMCParticleLink).
//
// The two-writer split is needed because the link emission depends on
// Tracking having already run (so ctx_.tracking is populated).

#pragma once

#include "delphi_edm4hep/CollectionWriter.h"
#include "delphi_edm4hep/Truth/TruthData.h"   // GenParticleResult

namespace delphi_edm4hep::truth {

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
