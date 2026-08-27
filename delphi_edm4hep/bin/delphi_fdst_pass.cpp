// delphi_fdst_pass — Pass-2 binary.
//
// Reads (a) the pass-1 intermediate edm4hep file and (b) a Delphi fullDST
// (.fadana) via PHDST. Drives the event loop on the fadana side. Per
// event, the harness looks up the matching intermediate frame by
// (run, evt) and uses it as the output frame so sDST_* collections are
// copied through automatically; pass-2 writers ADD fDST_* on top.
//
// Usage: delphi_fdst_pass <intermediate.edm4hep.root> <input.fadana>
//                        <output.edm4hep.root> [-n MAX]

#include "delphi_edm4hep/CollectionWriter.h"   // EventContext
#include "delphi_edm4hep/Btag/Btag.h"
#include "delphi_edm4hep/BtagMode.h"
#include "delphi_edm4hep/PhdstHarness.h"
#include "delphi_edm4hep/Calorimeter/CcalFdst.h"
#include "delphi_edm4hep/Calorimeter/EmcaFdst.h"
#include "delphi_edm4hep/Pid/FdstPidExtras.h"
#include "delphi_edm4hep/Calorimeter/HcalFdst.h"
#include "delphi_edm4hep/Tracking/MainHybrid.h"
#include "delphi_edm4hep/Tracking/MatchProvenance.h"
#include "delphi_edm4hep/Pid/MtpcFdst.h"
#include "delphi_edm4hep/Pid/PidHybrid.h"
#include "delphi_edm4hep/Calorimeter/ShowerHybrid.h"
#include "delphi_edm4hep/Calorimeter/SticShower.h"
#include "delphi_edm4hep/Truth/TblHybrid.h"
#include "delphi_edm4hep/Calorimeter/TdhaFdst.h"
#include "delphi_edm4hep/Tracking/TeStateMerge.h"
#include "delphi_edm4hep/Tracking/TraxFdst.h"
#include "delphi_edm4hep/Calorimeter/TeadFdst.h"
#include "delphi_edm4hep/Pid/TofFdst.h"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace harness = delphi_edm4hep::harness;

// PHDST user-hook overrides — must live in the binary TU. Forward
// straight to the harness (same pattern as delphi_sdst_pass).
extern "C" {
  void user00_() noexcept          { harness::on_user00();      }
  void user01_(int* need) noexcept { harness::on_user01(need);  }
  void user02_() noexcept          { harness::on_user02();      }
  void user99_() noexcept          { harness::on_user99();      }
}

static void usage(const char* argv0) {
  std::cerr
    << "usage: " << argv0
    << " <intermediate.edm4hep.root[,more.root...]> <input.fadana>"
    << " <output.edm4hep.root> [-n MAX_EVENTS]"
    << " [--btag off|bank|recalc] [--btag-pv]\n";
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
  if (argc < 4) { usage(argv[0]); return 1; }

  harness::Config cfg;
  // argv[1] may be a comma-separated list of intermediates. A long run's
  // official short-DST events span several .al tape files; pass all the
  // tapes that contain this run so every reconstructed event finds its
  // sDST match (otherwise segments whose events live in a non-listed
  // tape produce a 0-event file). First path is primary, rest are extra.
  {
    const std::string list = argv[1];
    size_t start = 0;
    while (start <= list.size()) {
      const size_t comma = list.find(',', start);
      const std::string one =
          list.substr(start, comma == std::string::npos ? std::string::npos
                                                         : comma - start);
      if (!one.empty()) {
        if (cfg.input_edm4hep.empty()) cfg.input_edm4hep = one;
        else cfg.input_edm4hep_extra.emplace_back(one);
      }
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
  }
  cfg.input         = argv[2];
  cfg.output        = argv[3];

  for (int i = 4; i < argc; ++i) {
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

  if (!std::filesystem::exists(cfg.input_edm4hep)) {
    std::cerr << "intermediate not found: " << cfg.input_edm4hep << "\n";
    return 1;
  }
  if (!std::filesystem::exists(cfg.input)) {
    std::cerr << "fdst input not found: " << cfg.input << "\n";
    return 1;
  }

  // Pass-2 writers. The harness already populated frame with the
  // matching intermediate's sDST_* collections; these writers ADD
  // fDST_* collections on top. EventContext threads cross-writer
  // state (e.g. fdst_pa_to_sdst_track from MatchProvenanceWriter
  // is consumed by later writers).
  cfg.on_event = [btag_mode = cfg.btag](podio::Frame& frame, int /*run*/, int /*evt*/) {
    using namespace delphi_edm4hep;
    EventContext ctx;
    // MatchProvenanceWriter must run FIRST: it populates
    // ctx.fdst_pa_to_sdst_particle (and _track) which the other
    // pass-2 writers consume for their setParticle linkage.
    matchprov::MatchProvenanceWriter(frame, ctx, "fDST").emit();
    te_merge::TeStateMergeWriter    (frame, ctx, "fDST").emit();
    emca_fdst::EmcaFdstWriter       (frame, ctx, "fDST").emit();
    hcal_fdst::HcalFdstWriter       (frame, ctx, "fDST").emit();
    tead_fdst::TeadFdstWriter       (frame, ctx, "fDST").emit();
    tdha_fdst::TdhaFdstWriter       (frame, ctx, "fDST").emit();
    stic_shower::SticShowerWriter   (frame, ctx, "fDST").emit();
    ccal_fdst::CcalFdstWriter       (frame, ctx, "fDST").emit();
    // ShowerHybrid clones sDST_EMNC/HCNC_Showers into fDST_* and must run
    // BEFORE MainHybrid: the Particle→Cluster relation lives on the
    // (mutable) particle, so MainHybrid re-points it onto these clones
    // while fDST_MAIN_Particles is still being built.
    shower_hybrid::ShowerHybridWriter(frame, ctx, "fDST").emit();
    // MainHybrid must run AFTER TeStateMerge (consumes fDST_TRAC_Tracks)
    // and ShowerHybrid (consumes fDST_EMNC/HCNC_Showers), and BEFORE the
    // hybrid writers that consume fDST_MAIN_Particles.
    main_hybrid::MainHybridWriter   (frame, ctx, "fDST").emit();
    // These PA ParticleID writers link setParticle() to fDST_MAIN_Particles
    // (1:1 with sDST_MAIN_Particles by clone index), so they MUST run AFTER
    // MainHybrid creates it. They previously ran earlier and linked to the
    // pass-1 sDST_MAIN_Particles, leaving final-file PID->particle relations
    // inconsistent with the PidHybrid-repointed clones. (MU/EL/TDID + TOF + MTPC + TRAX.)
    tof_fdst::TofFdstWriter         (frame, ctx, "fDST").emit();
    mtpc_fdst::MtpcFdstWriter       (frame, ctx, "fDST").emit();
    trax_fdst::TraxFdstWriter       (frame, ctx, "fDST").emit();
    fdst_pid_extras::FdstPidExtrasWriter(frame, ctx, "fDST").emit();
    pid_hybrid::PidHybridWriter     (frame, ctx, "fDST").emit();
    tbl_hybrid::TblHybridWriter     (frame, ctx, "fDST").emit();
    // B-tagging. fulldst=true: SKELANA recalculates for ANY IFLBTG > 0 on
    // a fullDST, so --btag bank still yields AABTAG-named output here.
    btag::BtagWriter                (frame, ctx, "fDST", btag_mode, /*fulldst=*/true).emit();
  };

  return harness::run(cfg);
}
