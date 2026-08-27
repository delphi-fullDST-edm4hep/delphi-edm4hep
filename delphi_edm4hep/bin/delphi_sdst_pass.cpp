// delphi_sdst_pass — Pass-1 binary.
//
// Reads a Delphi shortDST directly via PHDST and writes an intermediate
// edm4hep file containing the sDST_* collections.
//
// Usage: delphi_sdst_pass <input.sdst> <output.edm4hep.root> [-n MAX]

#include "delphi_edm4hep/CollectionWriter.h"   // EventContext
#include "delphi_edm4hep/Btag/Btag.h"
#include "delphi_edm4hep/BtagMode.h"
#include "delphi_edm4hep/PhdstHarness.h"
#include "delphi_edm4hep/Calorimeter/Calorimeter.h"
#include "delphi_edm4hep/Tracking/EltrSdst.h"
#include "delphi_edm4hep/Event/Event.h"
#include "delphi_edm4hep/Pid/ParticleId.h"
#include "delphi_edm4hep/Pid/PidExtrasSdst.h"
#include "delphi_edm4hep/Pid/SdstPaExtras.h"
#include "delphi_edm4hep/Calorimeter/SticShower.h"
#include "delphi_edm4hep/Tracking/VdHits.h"
#include "delphi_edm4hep/Tracking/Tracking.h"
#include "delphi_edm4hep/Truth/Truth.h"
#include "delphi_edm4hep/Vertex/Vertex.h"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

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
    << " <input.sdst> <output.edm4hep.root> [-n MAX_EVENTS]"
       " [--btag off|bank|recalc] [--btag-pv]\n";
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

// Parse --btag {off,bank,recalc}. Default is off: rerunning AABTAG is a
// reconstruction step, not a transcription, and it perturbs SKELANA track
// selection -- so it is opt-in.
static delphi_edm4hep::BtagMode parseBtag(const char* s, const char* argv0) {
  using delphi_edm4hep::BtagMode;
  if (std::strcmp(s, "off")    == 0) return BtagMode::Off;
  if (std::strcmp(s, "bank")   == 0) return BtagMode::Bank;
  if (std::strcmp(s, "recalc") == 0) return BtagMode::Recalc;
  std::cerr << "error: --btag expects off|bank|recalc, got '" << s << "'\n";
  usage(argv0);
  std::exit(1);
}

int main(int argc, char** argv) {
  if (argc < 3) { usage(argv[0]); return 1; }

  harness::Config cfg;
  cfg.input  = argv[1];
  cfg.output = argv[2];

  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
      cfg.max_events = parseMaxEvents(argv[++i], argv[0]);
    } else if (std::strcmp(argv[i], "--btag") == 0 && i + 1 < argc) {
      cfg.btag = parseBtag(argv[++i], argv[0]);
    } else if (std::strcmp(argv[i], "--btag-pv") == 0) {
      // Let AABTAG's vertex replace the DELANA one in PSCVTX (IFLPVT=1).
      // Off by default -- see BtagMode.h.
      cfg.btag_pv = delphi_edm4hep::BtagPrimaryVertex::Replace;
    } else {
      std::cerr << "unknown option: " << argv[i] << "\n";
      usage(argv[0]);
      return 1;
    }
  }

  // Per-event dispatch: Event scalars first, then Truth gen-particles
  // (since RecoToGen links need them), then Tracking (which Vertex /
  // V0 / PhotonConv depend on), then the RecoToGen link emission, then
  // Vertex. Source tag is "sDST" for the pass-1 output.
  cfg.on_event = [btag_mode = cfg.btag](podio::Frame& frame, int /*run*/, int /*evt*/) {
    delphi_edm4hep::EventContext ctx;

    // All writers (CollectionWriter base + ctx-mediated I/O).
    // Pipeline ordering: scalars first, then truth-gen, then tracks (so
    // ctx.tracking is set), then everything downstream that needs it.
    dom::event::EventWriter            (frame, ctx, "sDST").emit();
    dom::truth::TruthGenWriter         (frame, ctx, "sDST").emit();
    dom::tracking::TrackingWriter      (frame, ctx, "sDST").emit();
    dom::truth::TruthRecoLinkWriter    (frame, ctx, "sDST").emit();
    dom::vertex::VertexWriter          (frame, ctx, "sDST").emit();
    dom::calorimeter::CalorimeterWriter(frame, ctx, "sDST").emit();
    dom::particleid::ParticleIdWriter  (frame, ctx, "sDST").emit();
    // sDST-only PA extras: PHOT/ODHI ParticleID, SSTC STIC showers.
    dom::sdst_pa_extras::SdstPaExtrasWriter(frame, ctx, "sDST").emit();
    dom::stic_shower::SticShowerWriter     (frame, ctx, "sDST").emit();
    dom::eltr_sdst::EltrSdstWriter         (frame, ctx, "sDST").emit();
    // §3.3 deferred PSC commons: VD hits + VECP-indexed PID extras.
    dom::vd_hits::VdHitsWriter             (frame, ctx, "sDST").emit();
    dom::pid_extras_sdst::PidExtrasSdstWriter(frame, ctx, "sDST").emit();
    // B-tagging. After Tracking (needs ctx.tracking to resolve AABTAG's
    // PA addresses onto emitted Particles); no-op unless --btag was given.
    // fulldst=false: on a shortDST, SKELANA honours IFLBTG=1 as a bank read.
    dom::btag::BtagWriter(frame, ctx, "sDST", btag_mode, /*fulldst=*/false).emit();
  };

  return harness::run(cfg);
}
