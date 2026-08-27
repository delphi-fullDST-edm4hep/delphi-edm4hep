#include "delphi_edm4hep/PhdstHarness.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace harness = delphi_edm4hep::harness;

namespace {

int g_event_calls = 0;

}  // namespace

// These overrides deliberately exercise the same Fortran ABI boundary as the
// production converters. They must remain in this executable's translation
// unit because the DELPHI static archives also provide default userNN_ stubs.
extern "C" {
void user00_() noexcept { harness::on_user00(); }
void user01_(int* need) noexcept { harness::on_user01(need); }
void user02_() noexcept { harness::on_user02(); }
void user99_() noexcept { harness::on_user99(); }
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <input.sdst> <output.root>\n";
    return 64;
  }

  harness::Config cfg;
  cfg.input = std::filesystem::absolute(argv[1]);
  cfg.output = std::filesystem::absolute(argv[2]);
  cfg.max_events = 2;
  cfg.on_event = [](podio::Frame&, int, int) {
    ++g_event_calls;
    throw std::runtime_error("injected event failure");
  };
  cfg.on_finalize = [] {
    std::cerr << "harness_exception_probe: event_calls=" << g_event_calls
              << "\n";
  };

  return harness::run(cfg);
}
