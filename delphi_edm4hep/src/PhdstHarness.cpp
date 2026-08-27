// PhdstHarness.cpp — implementation S5.
//
// Drives the PHDST Fortran event loop in-process and dispatches per-event
// work to user-supplied hooks. PHDST is globally stateful (one instance
// per process), so the state here is `static` and the user*_ callbacks
// are Fortran-linkage entry points that the binary's overrides forward to.
//
// The binary's user00_..user99_ overrides must live in the *binary* TU
// (not in this library), because libphdstxx.a / libskelanaxx.a ship
// default stubs and a static-archive double definition would error.

#include "delphi_edm4hep/PhdstHarness.h"

#include "delphi_edm4hep/BtagMode.h"

#include "phdst/functions.hpp"   // ph::PHSET, phdst_, ph::IIIRUN etc.
#include "phdst/phciii.hpp"
#include "phdst/uxcom.hpp"
#include "phdst/uxlink.hpp"
#include "skelana/functions.hpp" // sk::psini_, sk::psbeg_
#include "skelana/pscflg.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

namespace ph = phdst;
namespace sk = skelana;

namespace delphi_edm4hep::harness {

namespace {

Config                            g_cfg;
std::unique_ptr<podio::ROOTWriter> g_writer;

// Pass-2 only: intermediate edm4hep reader(s) + (run, evt) -> (reader,
// entry) index. Built once at job start in on_user00. Multiple readers
// because a run's official short-DST events can span several .al tape
// files (see Config::input_edm4hep_extra); the index unions them.
std::vector<std::unique_ptr<podio::ROOTReader>>                   g_sdst_readers;
std::map<std::pair<int, int>, std::pair<unsigned, unsigned>>      g_sdst_index;

// (run, evt) of every event we have already disposed of this job —
// written OR deliberately skipped. The data fadana delivers each event
// as ~3 separate PHDST DST records (Records/DST ~ 3); without this we
// emit 3 identical frames per event. (run,evt) is unique per physical
// event, so this dedup is exact.
std::set<std::pair<int, int>>           g_processed;

// Pointer to the currently in-flight per-event Frame so any writer
// (or hook) needing read access during user02 can grab it.
const podio::Frame*                g_current_sdst_frame = nullptr;
long                               g_n_seen = 0;
long                               g_n_written = 0;
bool                               g_callback_failed = false;
std::string                        g_callback_failure;

// Configure SKELANA IFL* flags. These activate the standard DELPHI
// analysis processors and are copied verbatim from
// delphi-nanoaod/config/delphi-nanoaod.yaml (the same values the current
// SDST converter uses; bit-by-bit-compatible output).
void setSkelanaFlags(BtagMode btag, BtagPrimaryVertex btag_pv) {
  sk::IFLTRA = 1;
  sk::IFLODR = 1;
  sk::IFLVEC = 22;
  sk::IFLSTR = 11;
  sk::IFLCUT = 3;
  sk::IFLRVR = 111;
  sk::IFLSIM = 1;
  sk::IFLBSP = 2;
  // B-tagging is configurable (see BtagMode.h). Previously both of these
  // were hard-wired to IFLBTG=2 / IFLPVT=1, copied verbatim from
  // delphi-raw-nanoaod, which set them to reproduce legacy LVLOCK track
  // selection bit-for-bit -- not because b-tagging was wanted. The effect
  // was that AABTAG ran on every event, its output was never read by any
  // writer, and IFLPVT=1 let it overwrite the primary vertex (with -999
  // whenever the beamspot lookup failed).
  //
  // NOTE: changing IFLBTG away from 2 also changes SKELANA's track
  // selection, so output is no longer bit-compatible with delphi-nanoaod
  // at the default. That is intentional but must be validated, not
  // assumed -- see docs/README.md.
  switch (btag) {
    case BtagMode::Off:    sk::IFLBTG = 0; break;
    case BtagMode::Bank:   sk::IFLBTG = 1; break;
    case BtagMode::Recalc: sk::IFLBTG = 2; break;
  }
  sk::IFLPVT = (btag_pv == BtagPrimaryVertex::Replace) ? 1 : 0;
  sk::IFLVDR = 1;
  sk::IFLFCT = 1;
  sk::IFLRNQ = 0;
  sk::IFLBHP = 1;
  sk::IFLUTE = 1;
  sk::IFLVDH = 1;
  sk::IFLMUO = 1;
  sk::IFLECL = 22;
  sk::IFLELE = 1;
  sk::IFLEMC = 1;
  sk::IFLPHO = 1;
  sk::IFLPHC = 1;
  sk::IFLSTC = 1;
  sk::IFLHAC = 1;
  sk::IFLHAD = 1;
  sk::IFLRV0 = 1;
}

void recordCallbackFailure(const char* phase, const char* message) noexcept {
  const bool first_failure = !g_callback_failed;
  g_callback_failed = true;
  g_current_sdst_frame = nullptr;

  // Keep the first failure as the authoritative process diagnostic. A later
  // writer-finalization failure is useful to log, but must not hide the event
  // failure that made the output partial in the first place.
  if (first_failure) {
    try {
      g_callback_failure = phase ? phase : "callback";
      if (message && *message) {
        g_callback_failure += ": ";
        g_callback_failure += message;
      }
    } catch (...) {
      // The failure flag remains authoritative if retaining text allocates.
    }
  }

  // C stdio does not throw C++ exceptions. In particular, do not let an
  // iostream exception turn this noexcept containment path into terminate().
  std::fprintf(stderr,
               "delphi_edm4hep::harness: caught C++ %s failure%s%s\n",
               phase ? phase : "callback", message && *message ? ": " : "",
               message && *message ? message : "");
}

template <typename Callback>
void guardCallback(const char* phase, Callback&& callback) noexcept {
  try {
    std::forward<Callback>(callback)();
  } catch (const std::exception& error) {
    recordCallbackFailure(phase, error.what());
  } catch (...) {
    recordCallbackFailure(phase, nullptr);
  }
}

void finishWriterNoexcept(const char* phase) noexcept {
  if (!g_writer) return;
  guardCallback(phase, [] { g_writer->finish(); });
  g_writer.reset();
}

}  // namespace

void on_user00() noexcept {
  guardCallback("user00 initialization", [] {
    // Suppress FPE during SKELANA's borderline-track work — without this,
    // transient FPEs in PSHSCT/PSHBANKS would change IREJ outcomes.
    ph::PHSET("FPE", 0);
    sk::PSINI();
    setSkelanaFlags(g_cfg.btag, g_cfg.btag_pv);

    if (g_cfg.output.empty()) {
      std::cerr << "harness::on_user00: output path not set\n";
      std::exit(2);
    }
    g_writer = std::make_unique<podio::ROOTWriter>(g_cfg.output.string());
    std::cout << "delphi_edm4hep::harness: opened " << g_cfg.output << "\n";

    // Assemble the list of intermediate(s): the primary input_edm4hep
    // plus any extras to union with it.
    std::vector<std::filesystem::path> inters;
    if (!g_cfg.input_edm4hep.empty()) inters.push_back(g_cfg.input_edm4hep);
    for (const auto& p : g_cfg.input_edm4hep_extra) inters.push_back(p);

    if (!inters.empty()) {
      g_sdst_readers.reserve(inters.size());
      for (unsigned r = 0; r < inters.size(); ++r) {
        auto reader = std::make_unique<podio::ROOTReader>();
        reader->openFile(inters[r].string());

        // Build (run, evt) -> (reader, entry-idx) index by scanning all
        // frames. Reads Frame parameters only (collection deserialization
        // is lazy in podio, so this is cheap). First occurrence wins on
        // the (rare) duplicate key (begin-of-run records share evt across
        // tapes); emplace keeps the earliest reader.
        const unsigned N = reader->getEntries("events");
        unsigned added = 0;
        for (unsigned i = 0; i < N; ++i) {
          auto fd = reader->readEntry("events", i);
          if (!fd) break;
          const podio::Frame f(std::move(fd));
          const auto run = f.getParameter<int>("sDST_EVT_runNumber");
          const auto evt = f.getParameter<int>("sDST_EVT_eventNumber");
          if (run && evt) {
            if (g_sdst_index.emplace(std::make_pair(*run, *evt),
                                     std::make_pair(r, i)).second) {
              ++added;
            }
          }
        }
        std::cout << "delphi_edm4hep::harness: intermediate " << inters[r]
                  << " indexed (" << added << " new keys / " << N
                  << " entries)\n";
        g_sdst_readers.push_back(std::move(reader));
      }
      std::cout << "delphi_edm4hep::harness: " << g_sdst_readers.size()
                << " intermediate(s), " << g_sdst_index.size()
                << " unique (run, evt) keys total\n";
    }

    if (g_cfg.on_init) g_cfg.on_init();
  });
}

void on_user01(int* need) noexcept {
  if (!need) return;
  if (g_callback_failed) {
    *need = -3;  // stop after a caught C++ callback failure
    return;
  }
  if (g_cfg.max_events > 0 && g_n_written >= g_cfg.max_events) {
    *need = -3;   // stop the loop
    return;
  }
  *need = 1;
}

static void on_user02_impl() {
  ++g_n_seen;
  if (g_cfg.max_events > 0 && g_n_written >= g_cfg.max_events) return;

  sk::PSBEG();

  // Dedup. The data fullDST (.fadana) delivers each physical event as
  // ~3 separate PHDST DST records (Records/DST ~ 3.0); the .sdst/.al
  // streams deliver one. Without this guard we write 3 identical frames
  // per event. Recording both written AND skipped (run,evt) here means
  // any later re-delivery of the same event is a no-op, regardless of
  // record count. Placed before the first-event skip so a re-delivered
  // begin-of-run record can never slip through and get written.
  const std::pair<int, int> key{ph::IIIRUN, ph::IIIEVT};
  if (!g_processed.insert(key).second) return;

  // First-event-empty skip: DELSIM's event 1 is a setup record with no
  // PV chain; we drop it so the output event count == # physics events.
  if (g_n_seen == 1) {
    const bool no_pv_chain = (ph::LDTOP <= 0) || (ph::LQ(ph::LDTOP - 1) == 0);
    if (no_pv_chain) return;
  }

  podio::Frame frame;

  // Pass-2: look up the intermediate frame for this (run, evt) and
  // load it AS our output frame. Subsequent put() calls in the event
  // hook add fDST_* collections alongside the sDST_* already in the
  // frame. No copy-through writer needed — podio's writeFrame at the
  // end of user02 emits everything.
  //
  // If no matching intermediate exists, we SKIP this event rather
  // than writing an empty frame. Reasons:
  //   (a) pass-2's output is meant to be a strict superset of pass-1
  //       (sDST_* present); an event with no sDST_* would have a
  //       different schema and break podio's per-category consistency
  //       check.
  //   (b) pass-1's first-event-empty skip drops the (run, evt) of any
  //       DELSIM-empty sDST event 1. The corresponding fadana event 1
  //       isn't necessarily empty (the fullDST reconstruction may
  //       have produced PA banks where the condensed sDST didn't),
  //       so we'd hit a mismatch. Skipping keeps the two streams in
  //       lock-step on (run, evt).
  if (!g_sdst_readers.empty()) {
    auto it = g_sdst_index.find(key);
    if (it == g_sdst_index.end()) {
      if (g_n_seen <= 5) {
        std::cerr << "delphi_edm4hep::harness: skip — no sDST frame for"
                  << " (run=" << ph::IIIRUN
                  << ", evt=" << ph::IIIEVT << ")\n";
      }
      return;
    }
    auto fd = g_sdst_readers[it->second.first]
                  ->readEntry("events", it->second.second);
    if (fd) frame = podio::Frame(std::move(fd));
  }
  g_current_sdst_frame = &frame;

  if (g_cfg.on_event) {
    g_cfg.on_event(frame, ph::IIIRUN, ph::IIIEVT);
  }

  g_writer->writeFrame(frame, "events");
  g_current_sdst_frame = nullptr;
  ++g_n_written;

  if (g_n_written <= 5 || g_n_written % 500 == 0) {
    std::cout << "delphi_edm4hep::harness: event " << g_n_written
              << "  run=" << ph::IIIRUN
              << "  evt=" << ph::IIIEVT << "\n";
  }
}

void on_user02() noexcept {
  if (g_callback_failed) return;
  guardCallback("user02 event callback", [] { on_user02_impl(); });
}

void on_user99() noexcept {
  if (g_cfg.on_finalize) {
    guardCallback("user99 finalize hook", [] { g_cfg.on_finalize(); });
  }
  finishWriterNoexcept("user99 writer finalization");

  // The canonical success footer is consumed by campaign audits. Never emit
  // it for a failed/partial job, even though run() will also return nonzero.
  try {
    if (g_callback_failed) {
      std::cerr << "delphi_edm4hep::harness: aborted after " << g_n_written
                << " written events; partial output is not publishable\n";
    } else {
      std::cout << "delphi_edm4hep::harness: wrote " << g_n_written
                << " events to " << g_cfg.output << "\n";
    }
  } catch (...) {
    // Diagnostics are best effort; the failure state and exit code are not.
  }
}

const podio::Frame* currentSdstFrame() { return g_current_sdst_frame; }

int run(const Config& cfg) {
  g_cfg                  = cfg;
  g_n_seen               = 0;
  g_n_written            = 0;
  g_callback_failed      = false;
  g_callback_failure.clear();
  g_current_sdst_frame   = nullptr;
  g_writer.reset();
  g_sdst_readers.clear();
  g_sdst_index.clear();   // derived from the readers — reset together, so a
                          // second run() in one process can't use stale
                          // (run,evt) entry numbers against the new readers
  g_processed.clear();

  if (!std::filesystem::exists(cfg.input)) {
    std::cerr << "harness::run: input not found: " << cfg.input << "\n";
    return 1;
  }

  // PHDST reads its input via a fixed-format text file named PDLINPUT in cwd.
  // Long absolute paths are silently truncated by the legacy parser (and can
  // turn a valid input into a zero-event job), so put a short, process-unique
  // symlink in cwd and write only that relative name to PDLINPUT.
  std::error_code path_error;
  const auto abs_input = std::filesystem::absolute(cfg.input, path_error);
  if (path_error) {
    std::cerr << "harness::run: cannot resolve input path " << cfg.input
              << ": " << path_error.message() << "\n";
    return 1;
  }
  const auto input_link = std::filesystem::path(
      ".phdst_input_" + std::to_string(static_cast<long long>(::getpid())));
  if (std::filesystem::exists(input_link, path_error) || path_error) {
    std::cerr << "harness::run: short input link already exists or cannot be "
                 "inspected: "
              << input_link;
    if (path_error) std::cerr << ": " << path_error.message();
    std::cerr << "\n";
    return 1;
  }
  std::filesystem::create_symlink(abs_input, input_link, path_error);
  if (path_error) {
    std::cerr << "harness::run: cannot create short input link " << input_link
              << " -> " << abs_input << ": " << path_error.message() << "\n";
    return 1;
  }

  struct InputFileCleanup {
    std::filesystem::path link;
    bool remove_pdl_input = false;
    ~InputFileCleanup() {
      std::error_code ignored;
      if (remove_pdl_input) std::filesystem::remove("PDLINPUT", ignored);
      std::filesystem::remove(link, ignored);
    }
  } cleanup{input_link};

  const auto pdl_status = std::filesystem::symlink_status("PDLINPUT", path_error);
  if (path_error == std::errc::no_such_file_or_directory) {
    path_error.clear();
  } else if (path_error) {
    std::cerr << "harness::run: cannot inspect PDLINPUT: "
              << path_error.message() << "\n";
    return 1;
  } else if (pdl_status.type() != std::filesystem::file_type::not_found) {
    std::cerr << "harness::run: refusing to replace existing PDLINPUT\n";
    return 1;
  }
  {
    std::ofstream pdl("PDLINPUT");
    if (!pdl) {
      std::cerr << "harness::run: cannot create PDLINPUT in "
                << std::filesystem::current_path() << "\n";
      return 1;
    }
    cleanup.remove_pdl_input = true;
    pdl << "FILE = " << input_link.string() << "\n";
    if (!pdl) {
      std::cerr << "harness::run: cannot write PDLINPUT\n";
      return 1;
    }
  }

  // Drive the PHDST event loop. Empty option string -> default mode.
  // PHDST will invoke user00_ / user01_ / user02_ / user99_ as
  // the binary's overrides; those forward into our on_userNN above.
  int n = 0, m = 0;
  const char opt[] = " ";
  ph::phdst_(const_cast<char*>(opt), &n, &m, std::strlen(opt));

  // PHDST can report an input/open error yet return normally after producing a
  // nonempty metadata-only ROOT file. Make a zero-event conversion fail at the
  // process boundary so batch drivers cannot publish it as a valid product.
  finishWriterNoexcept("post-PHDST writer finalization");
  if (g_callback_failed) {
    std::cerr << "harness::run: event callback failed";
    if (!g_callback_failure.empty()) {
      std::cerr << ": " << g_callback_failure;
    }
    std::cerr << "\n";
    return 3;
  }
  if (g_n_written <= 0) {
    std::cerr << "harness::run: no events were written; conversion failed\n";
    return 2;
  }
  return 0;
}

}  // namespace delphi_edm4hep::harness
