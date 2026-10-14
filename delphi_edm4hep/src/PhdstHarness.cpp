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

#include "delphi_edm4hep/internal/DstCensus.h"

#include "delphi_edm4hep/CollectionWriter.h"
#include "delphi_edm4hep/Btag/BtagInfo.h"
#include "delphi_edm4hep/Event/EventInfo.h"

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
long                               g_n_no_dst = 0;
long                               g_n_redelivered = 0;
bool                               g_callback_failed = false;
std::string                        g_callback_failure;

// Configure SKELANA IFL* flags. These activate the standard DELPHI
// analysis processors and are copied verbatim from
// delphi-nanoaod/config/delphi-nanoaod.yaml (the same values the current
// SDST converter uses; bit-by-bit-compatible output).
void setSkelanaFlags() {
  sk::IFLTRA = 1;
  sk::IFLODR = 1;
  sk::IFLVEC = 22;
  sk::IFLSTR = 11;
  sk::IFLCUT = 3;
  sk::IFLRVR = 111;
  sk::IFLSIM = 1;
  sk::IFLBSP = 2;
  // AABTAG runs on every event while the remaining monolithic PSBEG sequence
  // is still in use. The converter no longer reads PSCBSP and reads the stored
  // BTAG bank directly; moving the recalculation itself requires splitting
  // PSBEG so AABTGS retains its exact position in the event sequence.
  // IFLPVT = 0 keeps SKELANA's primary vertex intact; AABTAG's own vertex is
  // emitted as a separate collection.
  sk::IFLBTG = 2;
  sk::IFLPVT = 0;
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
    setSkelanaFlags();
    event::initialize();

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
          const auto run = f.getParameter<int>(
              bank::make(bank::Pass::Sdst, "EVT", "runNumber"));
          const auto evt = f.getParameter<int>(
              bank::make(bank::Pass::Sdst, "EVT", "eventNumber"));
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

  // Records with no DST bank are not events. File headers and end-of-run
  // trailers reach this callback too, and can appear mid-file. They carry
  // no event of their own: the SKELANA commons still hold the preceding
  // event's values, so emitting one produces a frame whose event-level
  // quantities belong to a different event.
  if (ph::LDTOP <= 0) { ++g_n_no_dst; return; }

  // Dedup. The data fullDST (.fadana) delivers each physical event as
  // ~3 separate PHDST DST records (Records/DST ~ 3.0); the .sdst/.al
  // streams deliver one. Without this guard we write 3 identical frames
  // per event. Recording both written AND skipped (run,evt) here means
  // any later re-delivery of the same event is a no-op, regardless of
  // record count. Placed after the no-DST test so a header record cannot
  // consume the key of a later real event, and before the first-event skip
  // so a re-delivered begin-of-run record can never slip through.
  const std::pair<int, int> key{ph::IIIRUN, ph::IIIEVT};
  if (!g_processed.insert(key).second) { ++g_n_redelivered; return; }

  // DELSIM's event 1 is a setup record that has a DST bank but no PV chain;
  // drop it so the output event count equals the number of physics events.
  if (g_n_seen == 1 && ph::LQ(ph::LDTOP - 1) == 0) return;

  // Only query the direct DELPHI event services after the record guards.
  // Header/setup records can lack pilot blocklets such as DANA and must not
  // be treated as physics events.
  event::refresh();
  btag::refresh();

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

  // Record what this event's DST record carries, for the metadata frame.
  census::observeEvent();

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
  // Provenance is identical for every event, so it goes into a single
  // metadata frame rather than being repeated per event. Written before the
  // writer is finalized.
  guardCallback("user99 metadata frame", [] {
    if (!g_writer) return;
    const auto record = provenanceRecord();
    if (record.collections.empty()) return;
    podio::Frame meta;
    meta.putParameter("provenance_collection", record.collections);
    meta.putParameter("provenance_source",     record.sources);

    // What the input file actually carried, so an empty collection can be
    // told apart from a module the file never had. Covers the events
    // converted, not the whole file.
    meta.putParameter("dst_pa_modules_present",      census::paModules());
    meta.putParameter("dst_pilot_blocklets_present", census::pilotBlocklets());

    // The SKELANA track selection this file was produced with. IFLCUT names
    // the cut table (1 old SKELANA, 2 May-98 tuning, 3 April-99 tuning; the
    // tables are in the README). IFLSTR = 11 means rejected tracks are flagged
    // in VECP_LVLOCK, not removed.
    meta.putParameter("skelana_IFLCUT", sk::IFLCUT);
    meta.putParameter("skelana_IFLSTR", sk::IFLSTR);
    g_writer->writeFrame(meta, "metadata");
  });

  finishWriterNoexcept("user99 writer finalization");
  guardCallback("user99 provenance summary", [] { reportProvenance(); });

  // PHDST hands this callback every record, not every event. Report what was
  // dropped, so a change in the output count can be accounted for from the
  // job's own log rather than by comparing against PHDST's record footer.
  try {
    if (g_n_no_dst || g_n_redelivered) {
      std::cout << "delphi_edm4hep::harness: skipped " << g_n_no_dst
                << " records with no DST bank, " << g_n_redelivered
                << " re-delivered events\n";
    }
  } catch (...) {
  }

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
  g_n_no_dst             = 0;
  g_n_redelivered        = 0;
  g_callback_failed      = false;
  g_callback_failure.clear();
  g_current_sdst_frame   = nullptr;
  g_writer.reset();
  g_sdst_readers.clear();
  g_sdst_index.clear();   // derived from the readers — reset together, so a
                          // second run() in one process can't use stale
                          // (run,evt) entry numbers against the new readers
  g_processed.clear();

  // PHDST reads its input from a fixed-format text file named PDLINPUT in cwd.
  // Refuse to replace one we did not create; the cleanup below removes ours.
  std::error_code path_error;
  const auto pdl_status = std::filesystem::symlink_status("PDLINPUT", path_error);
  if (path_error == std::errc::no_such_file_or_directory) {
    path_error.clear();
  } else if (path_error) {
    std::cerr << "harness::run: cannot inspect PDLINPUT: "
              << path_error.message() << "\n";
    return 1;
  } else if (pdl_status.type() != std::filesystem::file_type::not_found) {
    std::cerr << "harness::run: PDLINPUT already exists in "
              << std::filesystem::current_path() << "; remove it and retry\n";
    return 1;
  }

  struct InputFileCleanup {
    std::filesystem::path link;
    bool remove_pdl_input = false;
    ~InputFileCleanup() {
      std::error_code ignored;
      if (remove_pdl_input) std::filesystem::remove("PDLINPUT", ignored);
      if (!link.empty()) std::filesystem::remove(link, ignored);
    }
  } cleanup;

  std::ofstream pdl;
  switch (cfg.input_mode) {
    case InputMode::File: {
      if (!std::filesystem::exists(cfg.input)) {
        std::cerr << "harness::run: input not found: " << cfg.input << "\n";
        return 1;
      }
      const auto abs_input = std::filesystem::absolute(cfg.input, path_error);
      if (path_error) {
        std::cerr << "harness::run: cannot resolve input path " << cfg.input
                  << ": " << path_error.message() << "\n";
        return 1;
      }
      // The legacy parser truncates paths over 120 characters, which turns a
      // valid input into a zero-event job. Point PDLINPUT at a short
      // process-unique symlink instead of the real path.
      const std::filesystem::path link(
          ".phdst_input_" + std::to_string(static_cast<long long>(::getpid())));
      if (std::filesystem::exists(link, path_error) || path_error) {
        std::cerr << "harness::run: short input link already exists or cannot "
                     "be inspected: " << link;
        if (path_error) std::cerr << ": " << path_error.message();
        std::cerr << "\n";
        return 1;
      }
      std::filesystem::create_symlink(abs_input, link, path_error);
      if (path_error) {
        std::cerr << "harness::run: cannot create short input link " << link
                  << " -> " << abs_input << ": " << path_error.message()
                  << "\n";
        return 1;
      }
      cleanup.link = link;

      pdl.open("PDLINPUT");
      if (!pdl) {
        std::cerr << "harness::run: cannot create PDLINPUT in "
                  << std::filesystem::current_path() << "\n";
        return 1;
      }
      cleanup.remove_pdl_input = true;
      pdl << "FILE = " << link.string() << "\n";
      break;
    }
    case InputMode::Nickname: {
      pdl.open("PDLINPUT");
      if (!pdl) {
        std::cerr << "harness::run: cannot create PDLINPUT in "
                  << std::filesystem::current_path() << "\n";
        return 1;
      }
      cleanup.remove_pdl_input = true;
      pdl << "FAT = " << cfg.input_nickname << "\n";
      break;
    }
    case InputMode::Pdl: {
      if (!std::filesystem::exists(cfg.input)) {
        std::cerr << "harness::run: pdl file not found: " << cfg.input << "\n";
        return 1;
      }
      std::filesystem::copy_file(cfg.input, "PDLINPUT",
                                 std::filesystem::copy_options::none,
                                 path_error);
      if (path_error) {
        std::cerr << "harness::run: cannot copy " << cfg.input
                  << " to PDLINPUT: " << path_error.message() << "\n";
        return 1;
      }
      cleanup.remove_pdl_input = true;
      break;
    }
  }
  if (cfg.input_mode != InputMode::Pdl) {
    pdl.close();
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
