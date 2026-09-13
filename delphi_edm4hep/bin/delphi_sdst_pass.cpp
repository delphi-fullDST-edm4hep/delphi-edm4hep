// delphi_sdst_pass — Pass-1 binary.
//
// Reads a Delphi shortDST directly via PHDST and writes an intermediate
// edm4hep file containing the sDST_* collections.
//
// Usage: delphi_sdst_pass <input.sdst> <output.edm4hep.root> [-n MAX]
//        delphi_sdst_pass -N|--nickname <nickname> <output.edm4hep.root> [-n MAX]
//        delphi_sdst_pass -P|--pdl <pdlinput> <output.edm4hep.root> [-n MAX]

#include "delphi_edm4hep/ConversionPipeline.h"
#include "delphi_edm4hep/PhdstHarness.h"
#ifdef DELPHI_SDST_SKELANA_REFERENCE
#include "delphi_edm4hep/internal/LegacySkelana.h"
#endif

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
// libdelphi_edm4hep.a), because the DELPHI archives ship default
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
  dom::pipeline::configureSdst(cfg);
#ifdef DELPHI_SDST_SKELANA_REFERENCE
  cfg.on_prepare_event = {};
  cfg.on_init = dom::legacy_skelana::initialize;
  cfg.on_record = dom::legacy_skelana::processRecord;
  cfg.event_info_supplied_by_record_hook = true;
#endif
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

  return harness::run(cfg);
}
