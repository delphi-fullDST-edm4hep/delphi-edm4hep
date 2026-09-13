// PidExtrasSdst domain — pass-1 only.
//
// Per-track DELPHI PID services that carry information beyond the four
// standard pass-1 PID collections. Bound to particles by their raw PA.
//
// The tag collections are recomputed at conversion time rather than read from
// the DST, so each is named for the routine that fills it. RPRODO and RPRODE
// are two generations of the same tagging; the PXDST version word of the event
// selects one, and only that one is emitted.
//
// Emits (ParticleIDCollection, setParticle -> sDST_MAIN_Particles):
//   sDST_XNEWTAG_RichTags     (algType=41) RICH ring/veto tags
//     params = KHAIDN(1..4) pion/kaon/proton/heavy tag
//              KHAIDT(1..4) matching track-quality acceptance
//   sDST_XNEWPRO_RichTags     (algType=42) RICH RIBMEAN probability tags
//     params = KHAIDR(1..6) pion/kaon/proton/heavy/electron tag, selection flag
//   sDST_RPRODO_DedxTags      (algType=43) TPC dE/dx tags, PXDST version < 333
//   sDST_RPRODE_DedxTags      (algType=44) TPC dE/dx tags, PXDST version >= 333
//     params = KHAIDE(1..6) pion/kaon/proton/heavy/electron tag, quality flag
//   sDST_RPROCO_CombinedTags  (algType=45) combined RICH and dE/dx tags
//     params = KHAIDC(1..6) pion/kaon/proton/heavy/electron tag, selection flag
//   sDST_HAID_dEdxVD          (algType=7)   VD-only dE/dx (PSCDEX VD slots)
//     params = QDEDX(6) VD dE/dx, KDEDX(7) n VD hits; emitted if nVD > 0
//   sDST_PHOT_Pi0ID           (algType=111) π⁰→γγ HPCANA fit (PSCPI0, 26 fields)
//     params = QPI0/KPI0(1..26); emitted if the fit mass QPI0(1) > 0
//
// Tag values are -1 for no information and 0..3 for increasing tightness. A tag
// row is emitted for a track when at least one of its tags is not -1.
//
// All are charged-track commons except Pi0, which is scanned over the full
// VECP range (π⁰ candidates can sit on neutral PAs).

#pragma once

#include "delphi_edm4hep/CollectionWriter.h"

namespace delphi_edm4hep::pid_extras_sdst {

class PidExtrasSdstWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;
  void emit() override;
};

}  // namespace delphi_edm4hep::pid_extras_sdst
