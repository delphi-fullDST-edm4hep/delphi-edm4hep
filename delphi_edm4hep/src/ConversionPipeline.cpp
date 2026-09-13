#include "delphi_edm4hep/ConversionPipeline.h"

#include "delphi_edm4hep/Btag/Btag.h"
#include "delphi_edm4hep/Calorimeter/Calorimeter.h"
#include "delphi_edm4hep/Calorimeter/Emca.h"
#include "delphi_edm4hep/Calorimeter/HcalFdst.h"
#include "delphi_edm4hep/Calorimeter/ShowerHybrid.h"
#include "delphi_edm4hep/Calorimeter/SticInfo.h"
#include "delphi_edm4hep/Calorimeter/SticShower.h"
#include "delphi_edm4hep/Calorimeter/Tdha.h"
#include "delphi_edm4hep/Calorimeter/TeadFdst.h"
#include "delphi_edm4hep/CollectionWriter.h"
#include "delphi_edm4hep/Event/Event.h"
#include "delphi_edm4hep/Event/EventInfo.h"
#include "delphi_edm4hep/PhdstHarness.h"
#include "delphi_edm4hep/Pid/Mtpc.h"
#include "delphi_edm4hep/Pid/PaPidExtras.h"
#include "delphi_edm4hep/Pid/ParticleId.h"
#include "delphi_edm4hep/Pid/PidExtrasSdst.h"
#include "delphi_edm4hep/Pid/PidHybrid.h"
#include "delphi_edm4hep/Pid/SdstPaExtras.h"
#include "delphi_edm4hep/Pid/Tof.h"
#include "delphi_edm4hep/Tracking/EltrSdst.h"
#include "delphi_edm4hep/Tracking/MainHybrid.h"
#include "delphi_edm4hep/Tracking/MatchProvenance.h"
#include "delphi_edm4hep/Tracking/TrackElements.h"
#include "delphi_edm4hep/Tracking/TrackHybrid.h"
#include "delphi_edm4hep/Tracking/Tracking.h"
#include "delphi_edm4hep/Tracking/Trax.h"
#include "delphi_edm4hep/Tracking/VdHits.h"
#include "delphi_edm4hep/Tracking/VftHits.h"
#include "delphi_edm4hep/Truth/TblHybrid.h"
#include "delphi_edm4hep/Truth/Truth.h"
#include "delphi_edm4hep/Vertex/Vertex.h"

namespace delphi_edm4hep::pipeline {

void configureSdst(harness::Config& cfg) {
  cfg.on_prepare_event = [] {
    event::repairSecondaryHadronicInteractions();
    btag::recalculate();
    stic::refreshFromSdst();
  };

  cfg.on_event = [](podio::Frame& frame, int, int) {
    EventContext ctx;
    event::EventWriter(frame, ctx, bank::Pass::Sdst).emit();
    truth::TruthGenWriter(frame, ctx, bank::Pass::Sdst).emit();
    track_elements::TrackElementsWriter(frame, ctx, bank::Pass::Sdst).emit();
    trax::TraxWriter(frame, ctx, bank::Pass::Sdst).emit();
    vd_hits::VdHitsWriter(frame, ctx, bank::Pass::Sdst).emit();
    tracking::TrackingWriter(frame, ctx, bank::Pass::Sdst).emit();
    truth::TruthRecoLinkWriter(frame, ctx, bank::Pass::Sdst).emit();
    vertex::VertexWriter(frame, ctx, bank::Pass::Sdst).emit();
    emca::EmcaWriter(frame, ctx, bank::Pass::Sdst).emit();
    calorimeter::CalorimeterWriter(frame, ctx, bank::Pass::Sdst).emit();
    particleid::ParticleIdWriter(frame, ctx, bank::Pass::Sdst).emit();
    sdst_pa_extras::SdstPaExtrasWriter(frame, ctx, bank::Pass::Sdst).emit();
    stic_shower::SticShowerWriter(frame, ctx, bank::Pass::Sdst).emit();
    eltr_sdst::EltrSdstWriter(frame, ctx, bank::Pass::Sdst).emit();
    vft_hits::VftHitsWriter(frame, ctx, bank::Pass::Sdst).emit();
    pid_extras_sdst::PidExtrasSdstWriter(frame, ctx, bank::Pass::Sdst).emit();
    mtpc::MtpcWriter(frame, ctx, bank::Pass::Sdst).emit();
    tof::TofWriter(frame, ctx, bank::Pass::Sdst).emit();
    pa_pid_extras::PaPidExtrasWriter(frame, ctx, bank::Pass::Sdst).emit();
    tdha::TdhaWriter(frame, ctx, bank::Pass::Sdst).emit();
    btag::BtagWriter(frame, ctx, bank::Pass::Sdst).emit();
  };
}

void configureFdst(harness::Config& cfg) {
  cfg.on_prepare_event = [] {
    btag::recalculate();
    stic::refreshFromFullDst();
  };

  cfg.on_event = [](podio::Frame& frame, int, int) {
    EventContext ctx;
    matchprov::MatchProvenanceWriter(frame, ctx, bank::Pass::Fdst).emit();
    track_elements::TrackElementsWriter(frame, ctx, bank::Pass::Fdst).emit();
    trax::TraxWriter(frame, ctx, bank::Pass::Fdst).emit();
    track_hybrid::TrackHybridWriter(frame, ctx, bank::Pass::Fdst).emit();
    emca::EmcaWriter(frame, ctx, bank::Pass::Fdst).emit();
    hcal_fdst::HcalFdstWriter(frame, ctx, bank::Pass::Fdst).emit();
    tead_fdst::TeadFdstWriter(frame, ctx, bank::Pass::Fdst).emit();
    tdha::TdhaWriter(frame, ctx, bank::Pass::Fdst).emit();
    stic_shower::SticShowerWriter(frame, ctx, bank::Pass::Fdst).emit();
    shower_hybrid::ShowerHybridWriter(frame, ctx, bank::Pass::Fdst).emit();
    main_hybrid::MainHybridWriter(frame, ctx, bank::Pass::Fdst).emit();
    tof::TofWriter(frame, ctx, bank::Pass::Fdst).emit();
    mtpc::MtpcWriter(frame, ctx, bank::Pass::Fdst).emit();
    pa_pid_extras::PaPidExtrasWriter(frame, ctx, bank::Pass::Fdst).emit();
    pid_hybrid::PidHybridWriter(frame, ctx, bank::Pass::Fdst).emit();
    tbl_hybrid::TblHybridWriter(frame, ctx, bank::Pass::Fdst).emit();
    btag::BtagWriter(frame, ctx, bank::Pass::Fdst).emit();
  };
}

}  // namespace delphi_edm4hep::pipeline
