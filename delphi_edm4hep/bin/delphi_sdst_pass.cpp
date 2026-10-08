// delphi_sdst_pass — Pass-1 binary.
//
// Reads a Delphi shortDST directly via PHDST and writes an intermediate
// edm4hep file containing the sDST_* collections.
//
// Usage: delphi_sdst_pass <input.sdst> <output.edm4hep.root> [-n MAX]
//        delphi_sdst_pass -N|--nickname <nickname> <output.edm4hep.root> [-n MAX]
//        delphi_sdst_pass -P|--pdl <pdlinput> <output.edm4hep.root> [-n MAX]

#include "delphi_edm4hep/internal/BeamSpotStatus.h"
#include "delphi_edm4hep/CollectionWriter.h"   // EventContext
#include "delphi_edm4hep/Btag/Btag.h"
#include "delphi_edm4hep/PhdstHarness.h"
#include "delphi_edm4hep/Calorimeter/Calorimeter.h"
#include "delphi_edm4hep/Tracking/EltrSdst.h"
#include "delphi_edm4hep/Event/Event.h"
#include "delphi_edm4hep/Pid/ParticleId.h"
#include "delphi_edm4hep/Pid/PidExtrasSdst.h"
#include "delphi_edm4hep/Pid/SdstPaExtras.h"
#include "delphi_edm4hep/Calorimeter/SticShower.h"
#include "delphi_edm4hep/Tracking/VdHits.h"
#include "delphi_edm4hep/Tracking/VftHits.h"
#include "delphi_edm4hep/Calorimeter/Emca.h"
#include "delphi_edm4hep/Calorimeter/Tdha.h"
#include "delphi_edm4hep/Pid/Mtpc.h"
#include "delphi_edm4hep/Pid/PaPidExtras.h"
#include "delphi_edm4hep/Pid/Tof.h"
#include "delphi_edm4hep/Tracking/Trax.h"
#include "delphi_edm4hep/Tracking/TrackElements.h"
#include "delphi_edm4hep/Tracking/Tracking.h"
#include "delphi_edm4hep/Truth/Truth.h"
#include "delphi_edm4hep/Vertex/Vertex.h"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace harness = delphi_edm4hep::harness;
namespace dom     = delphi_edm4hep;

// PHDST user-hook overrides. These MUST live in the binary TU (not in
// libdelphi_edm4hep.a), because libphdstxx.a / libskelanaxx.a ship default
// stubs and a double-archive-definition would error at link time.
// We forward into the harness which dispatches to the configured hooks.
extern "C" {
  void user00_() noexcept          { harness::on_user00();        }
  void user01_(int* need) noexcept { harness::on_user01(need);    }
  void user02_() noexcept          { harness::on_user02();        }
  void user99_() noexcept          { harness::on_user99();        }
}

static void usage(const char* argv0) {
  std::cerr
    << "usage: " << argv0
    << " <input.sdst> <output.edm4hep.root> [-n MAX_EVENTS]\n"
    << "       " << argv0
    << " -N|--nickname <nickname> <output.edm4hep.root> [-n MAX_EVENTS]\n"
    << "       " << argv0
    << " -P|--pdl <pdlinput> <output.edm4hep.root> [-n MAX_EVENTS]\n";
}

// Parse a strictly-positive integer for -n; error + usage + exit(1) on
// non-numeric input, <= 0, or overflow. (std::atoi silently returned 0 on
// "-n abc"/"-n 0", which max_events<=0 then treated as "unlimited".)
static int parseMaxEvents(const char* s, const char* argv0) {
  int v = 0;
  const char* end = s + std::strlen(s);
  const auto res = std::from_chars(s, end, v);
  if (res.ec != std::errc{} || res.ptr != end || v <= 0) {
    std::cerr << "error: -n expects a positive integer, got '" << s << "'\n";
    usage(argv0);
    std::exit(1);
  }
  return v;
}

int main(int argc, char** argv) {
  if (argc < 2) { usage(argv[0]); return 1; }

  harness::Config cfg;
  std::vector<std::string> positional;
  bool have_input_mode = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if ((arg == "-N" || arg == "--nickname") && i + 1 < argc) {
      if (have_input_mode) {
        std::cerr << "error: only one of <input.sdst>, -N/--nickname,"
                     " -P/--pdl may be given\n";
        usage(argv[0]);
        return 1;
      }
      cfg.input_mode     = harness::InputMode::Nickname;
      cfg.input_nickname = argv[++i];
      have_input_mode    = true;
    } else if ((arg == "-P" || arg == "--pdl") && i + 1 < argc) {
      if (have_input_mode) {
        std::cerr << "error: only one of <input.sdst>, -N/--nickname,"
                     " -P/--pdl may be given\n";
        usage(argv[0]);
        return 1;
      }
      cfg.input_mode  = harness::InputMode::Pdl;
      cfg.input       = argv[++i];
      have_input_mode = true;
    } else if (arg == "-n" && i + 1 < argc) {
      cfg.max_events = parseMaxEvents(argv[++i], argv[0]);
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "unknown option: " << arg << "\n";
      usage(argv[0]);
      return 1;
    } else {
      positional.push_back(arg);
    }
  }

  if (have_input_mode) {
    if (positional.size() != 1) { usage(argv[0]); return 1; }
    cfg.output = positional[0];
  } else {
    if (positional.size() != 2) { usage(argv[0]); return 1; }
    cfg.input  = positional[0];
    cfg.output = positional[1];
  }

  // Per-event dispatch: Event scalars first, then Truth gen-particles
  // (since RecoToMC links need them), then Tracking (which Vertex /
  // V0 / PhotonConv depend on), then the RecoToMC link emission, then
  // Vertex. Writers run under Pass::Sdst; the prefix on each
  // collection follows its bank.
  cfg.on_event = [](podio::Frame& frame, int /*run*/, int /*evt*/) {
    delphi_edm4hep::beamspot::requireDatabase();
    delphi_edm4hep::EventContext ctx;

    // All writers (CollectionWriter base + ctx-mediated I/O).
    // Pipeline ordering: scalars first, then truth-gen, then tracks (so
    // ctx.tracking is set), then everything downstream that needs it.
    dom::event::EventWriter            (frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::truth::TruthGenWriter         (frame, ctx, dom::bank::Pass::Sdst).emit();
    // TrackElements runs before Tracking so the mother tracks can link to
    // the track elements while they are still mutable.
    dom::track_elements::TrackElementsWriter(frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::trax::TraxWriter                  (frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::vd_hits::VdHitsWriter         (frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::tracking::TrackingWriter      (frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::truth::TruthRecoLinkWriter    (frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::vertex::VertexWriter          (frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::emca::EmcaWriter               (frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::calorimeter::CalorimeterWriter(frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::particleid::ParticleIdWriter  (frame, ctx, dom::bank::Pass::Sdst).emit();
    // sDST-only PA extras: PHOT/ODHI ParticleID, SSTC STIC showers.
    dom::sdst_pa_extras::SdstPaExtrasWriter(frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::stic_shower::SticShowerWriter     (frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::eltr_sdst::EltrSdstWriter         (frame, ctx, dom::bank::Pass::Sdst).emit();
    // §3.3 deferred PSC commons: VD hits + VECP-indexed PID extras.
    dom::vft_hits::VftHitsWriter           (frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::pid_extras_sdst::PidExtrasSdstWriter(frame, ctx, dom::bank::Pass::Sdst).emit();

    // PA modules that are on the (X)shortDST as well as the fullDST. Each is
    // empty when its module is absent, which depends on the processing rather
    // than on the era -- see the availability table in the README.
    dom::mtpc::MtpcWriter                (frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::tof::TofWriter                  (frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::pa_pid_extras::PaPidExtrasWriter(frame, ctx, dom::bank::Pass::Sdst).emit();
    dom::tdha::TdhaWriter                (frame, ctx, dom::bank::Pass::Sdst).emit();
    // B-tagging. After Tracking (needs ctx.tracking to resolve AABTAG's
    // PA addresses onto emitted Particles).
    dom::btag::BtagWriter(frame, ctx, dom::bank::Pass::Sdst).emit();
  };

  return harness::run(cfg);
}
