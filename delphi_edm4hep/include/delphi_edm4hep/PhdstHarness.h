// PhdstHarness.h
//
// Thin wrapper around the PHDST event loop (user00 / user01 / user02 /
// user99 Fortran callbacks). Owns the per-job podio writer and per-event
// Frame builder. Single-instance because PHDST is globally stateful —
// one PHDST per process.
//
// The binary's user00_/01_/02_/99_ Fortran overrides forward into the
// `on_user00/01/02/99` entry points exposed here. We can't put those
// overrides in the library itself because libphdstxx.a / libskelanaxx.a
// ship default-stub implementations and a static-archive double-supply
// of the same symbol breaks linking.
//

#pragma once

#include "delphi_edm4hep/BtagMode.h"

#include <podio/Frame.h>
#include <podio/ROOTReader.h>
#include <podio/ROOTWriter.h>

#include <filesystem>
#include <functional>
#include <vector>

namespace delphi_edm4hep::harness {

// Per-event hook. Called once per kept event from user02_, AFTER PSBEG
// has populated the SKELANA commons. The hook fills `frame` from
// whichever domain modules it needs.
using EventHook = std::function<void(podio::Frame& frame, int run, int evt)>;

// Per-job init hook (after PSINI + IFL flags + writer open) and per-job
// teardown hook (before writer.finish()). Both optional.
using InitHook     = std::function<void()>;
using FinalizeHook = std::function<void()>;

struct Config {
  std::filesystem::path input;            // Delphi .sdst / .fadana / .al
  std::filesystem::path output;           // edm4hep output (podio writes)
  std::filesystem::path input_edm4hep;    // pass-2 only: intermediate to copy through; empty for pass 1
  // pass-2 only: ADDITIONAL intermediates whose (run,evt) indices are
  // unioned with input_edm4hep. A long run's official short-DST events
  // are spread across several .al tape files; each maps to its own
  // intermediate, and a single raw segment of that run can match events
  // in any of them. Without the union, segments whose events live in a
  // non-indexed tape match nothing and produce a 0-event (metadata-only)
  // file. (run,evt) is globally unique, so the union has no real
  // collisions; first-occurrence wins.
  std::vector<std::filesystem::path> input_edm4hep_extra;
  int                   max_events = -1;  // -1 = unlimited

  // DELPHI b-tagging. Off by default: rerunning AABTAG is a new
  // reconstruction, not a transcription of the DST, and it is only worth
  // its cost when the b-tag output is actually wanted.
  BtagMode          btag    = BtagMode::Off;
  // Whether AABTAG's vertex replaces the DELANA one in PSCVTX. Keep by
  // default -- see BtagMode.h for why Replace is dangerous.
  BtagPrimaryVertex btag_pv = BtagPrimaryVertex::Keep;

  InitHook     on_init;
  EventHook    on_event;
  FinalizeHook on_finalize;
};

// Run the PHDST event loop with `cfg`. Creates a short cwd-local symlink to
// cfg.input, writes that relative name to PDLINPUT (the legacy fixed-format
// parser truncates long absolute paths), drives phdst_(), and blocks until
// done. Returns 0 only when at least one event was written.
int run(const Config& cfg);

// User-callback forwarders. The binary's extern "C" user*_ overrides
// invoke these. They consult the static config + state set by `run`.
// Every forwarder is noexcept. Throwing init, event, finalize, or writer code
// is converted into a controlled job failure before control returns through
// an extern "C" userNN_ callback into PHDST/Fortran.
void on_user00() noexcept;
void on_user01(int* need) noexcept;
void on_user02() noexcept;
void on_user99() noexcept;

// Accessor for the pass-1 intermediate frame (when cfg.input_edm4hep is
// non-empty). Returns nullptr otherwise or before user02 has loaded the
// matching frame. Valid for the duration of the current event hook.
const podio::Frame* currentSdstFrame();

}  // namespace delphi_edm4hep::harness
