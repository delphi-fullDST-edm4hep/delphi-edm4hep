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

#include "delphi_edm4hep/ConversionPipeline.h"
#include "delphi_edm4hep/PhdstHarness.h"

#if defined(DELPHI_FDST_SKELANA_REFERENCE) || \
    defined(DELPHI_FDST_SKELANA_INIT_REFERENCE)
#include "delphi_edm4hep/internal/LegacySkelana.h"
#endif

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
    << " <output.edm4hep.root> [-n MAX_EVENTS]\n";
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
  if (argc < 4) { usage(argv[0]); return 1; }

  harness::Config cfg;
  delphi_edm4hep::pipeline::configureFdst(cfg);
#ifdef DELPHI_FDST_SKELANA_REFERENCE
  cfg.on_prepare_event = {};
  cfg.on_init = delphi_edm4hep::legacy_skelana::initialize;
  cfg.on_record = delphi_edm4hep::legacy_skelana::processRecord;
  cfg.event_info_supplied_by_record_hook = true;
#elif defined(DELPHI_FDST_SKELANA_INIT_REFERENCE)
  // Migration-only diagnostic: retain PSINI's one-time side effects, but
  // prepare every event through the converter-owned readers. Comparing this
  // executable with the full PSBEG oracle identifies whether a discrepancy
  // belongs to initialization or to SKELANA's per-event processing graph.
  cfg.on_init = delphi_edm4hep::legacy_skelana::initialize;
#endif
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

  return harness::run(cfg);
}
