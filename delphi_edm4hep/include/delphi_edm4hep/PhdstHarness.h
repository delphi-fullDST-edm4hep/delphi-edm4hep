// PhdstHarness.h
//
// Thin wrapper around the PHDST event loop (user00 / user01 / user02 /
// user99 Fortran callbacks). Owns the per-job podio writer and per-event
// Frame builder. Single-instance because PHDST is globally stateful —
// one PHDST per process.
//
// The binary's user00_/01_/02_/99_ Fortran overrides forward into the
// `on_user00/01/02/99` entry points exposed here. We can't put those
// overrides in the library itself because the DELPHI archives ship
// default-stub implementations and a static-archive double-supply
// of the same symbol breaks linking.
//

#pragma once


#include <podio/Frame.h>
#include <podio/ROOTReader.h>
#include <podio/ROOTWriter.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace delphi_edm4hep::harness {

// Per-event hook. Called once per kept event from user02_, after the optional
// record hook and the converter-owned event services have run. The hook fills
// `frame` from whichever domain modules it needs.
using EventHook = std::function<void(podio::Frame& frame, int run, int evt)>;

// Optional destination for an in-memory frame. Returning true acknowledges
// that the frame was consumed; false asks PHDST to stop cleanly. This is the
// native Code4hep boundary: the source can publish a frame directly without a
// temporary podio file while the standalone tools continue to use `output`.
using FrameSink = std::function<bool(podio::Frame&& frame, int run, int evt)>;

// Optional per-job initialization, per-PHDST-record preparation, and per-job
// teardown hooks. The record hook runs before the no-DST/header guards because
// reference processors must see every record. Direct per-event calculations
// belong in PrepareEventHook, after those guards.
using InitHook     = std::function<void()>;
using RecordHook   = std::function<void()>;
using PrepareEventHook = std::function<void()>;
using FinalizeHook = std::function<void()>;

// How `run()` builds PDLINPUT (PHDST's cwd input directive file):
//   File     — `input` is a Delphi .sdst/.fadana/.al file; writes a
//              "FILE = <abs path>" directive.
//   Nickname — `input_nickname` is a DELPHI dataset nickname (e.g.
//              "short94_c2" or "short94_c2/c1-10"); writes a
//              "FAT = <nickname>" directive so PHDST's own dataset
//              lookup resolves it.
//   Pdl      — `input` is a pre-built PDL file (e.g. from `fatfind`);
//              copied verbatim to PDLINPUT.
enum class InputMode { File, Nickname, Pdl };

struct Config {
  std::filesystem::path input;            // File: Delphi .sdst/.fadana/.al; Pdl: path to the PDL file to copy
  std::string           input_nickname;   // Nickname: the DELPHI dataset nickname
  InputMode             input_mode = InputMode::File;
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

  // Values recorded under the historical metadata keys
  // `skelana_IFLCUT`/`skelana_IFLSTR`. They describe the particle selection
  // contract, independent of which implementation supplied it.
  int                   selection_cut = 3;
  int                   selection_mode = 11;

  // Optional reference adapters may snapshot PSHEVT/PSBEAM into EventInfo.
  // In that mode a second direct refresh would call VDBSPT twice and change
  // the simulated beamspot random sequence.
  bool                  event_info_supplied_by_record_hook = false;

  InitHook     on_init;
  RecordHook   on_record;
  // Runs after direct event/beamspot/BTAG-bank refresh and before writers.
  // Full-DST uses this for direct package calculations that need EventInfo.
  PrepareEventHook on_prepare_event;
  EventHook    on_event;
  FrameSink    frame_sink;
  FinalizeHook on_finalize;
};

// Run the PHDST event loop with `cfg`. Writes a PDLINPUT in cwd per
// cfg.input_mode (see InputMode above), drives phdst_(), and blocks until
// done. File mode points PDLINPUT at a short cwd-local symlink rather than
// the real path, because the legacy fixed-format parser truncates long ones.
// Exactly one destination must be configured: `output` for a podio file or
// `frame_sink` for in-memory delivery. Returns 0 only when at least one event
// was delivered to that destination.
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
