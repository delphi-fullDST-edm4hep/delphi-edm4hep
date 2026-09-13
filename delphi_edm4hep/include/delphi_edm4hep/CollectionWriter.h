// CollectionWriter.h
//
// Abstract base for per-domain podio writers. Owns a reference to the
// Frame, an EventContext (shared scratch space across writers in one
// event), and which conversion pass is running. Subclasses implement
// `emit()`; the protected `put` / `putParameter` helpers build the
// canonical "<prefix>_<bank>_<readable>" collection / parameter name and
// forward to the frame. BankPrefix.h decides the prefix from the bank and
// the pass.
//
// Per-event flow:
//   EventContext ctx;
//   EventWriter        (frame, ctx, Pass::Sdst).emit();  // EVT_* params
//   TruthGenWriter     (frame, ctx, Pass::Sdst).emit();  // sets ctx.gen_truth
//   TrackingWriter     (frame, ctx, Pass::Sdst).emit();  // sets ctx.tracking
//   TruthRecoLinkWriter(frame, ctx, Pass::Sdst).emit();  // reads both
//   VertexWriter       (frame, ctx, Pass::Sdst).emit();  // reads ctx.tracking
//   CalorimeterWriter  (frame, ctx, Pass::Sdst).emit();  // reads ctx.tracking
//   ParticleIdWriter   (frame, ctx, Pass::Sdst).emit();  // reads ctx.tracking
//
// All writers are stack-allocated; no heap, no vtable dispatch in the
// usual case. `emit()` is virtual to leave the door open for runtime
// opt-out (e.g. --skip-calorimeter) but the dispatch overhead is just
// one indirect call per event per domain — negligible.

#pragma once

// Data-only headers (no writer classes) so this header doesn't pull
// CollectionWriter back in transitively.
#include "delphi_edm4hep/Tracking/TrackElementData.h" // track_elements::Output
#include "delphi_edm4hep/Tracking/TraxData.h"         // trax::Output
#include "delphi_edm4hep/Tracking/TrackingData.h"   // tracking::Output
#include "delphi_edm4hep/Tracking/VdHitData.h"      // vd_hits::Output
#include "delphi_edm4hep/Calorimeter/EmcaData.h"    // emca::Output
#include "delphi_edm4hep/Truth/TruthData.h"      // truth::GenParticleResult
#include "delphi_edm4hep/BankPrefix.h"           // bank::Pass, bank::make

#include <edm4hep/ReconstructedParticleCollection.h>
#include <podio/Frame.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace delphi_edm4hep {

// Per-event shared state — passed by reference into every writer's ctor.
// Each writer reads its inputs from here (where another writer earlier
// in the chain set them) and stores its outputs here (where a later
// writer will read them). std::optional members signal "not yet set".
struct EventContext {
  // Set by TruthGenWriter; consumed by TruthRecoLinkWriter.
  std::optional<truth::GenParticleResult> gen_truth;

  // Set by TrackElementsWriter; consumed by TrackingWriter, which links each
  // mother track to the track elements from the same PA.
  std::optional<track_elements::Output> track_elements;

  // Set by TraxWriter; consumed by the track writers, which append the
  // extrapolation states to the track built from the same PA.
  std::optional<trax::Output> trax;

  // Set by VdHitsWriter; consumed by TrackingWriter, which links each track
  // to the Vertex-Detector hits assigned to it.
  std::optional<vd_hits::Output> vd_hits;

  // Set by EmcaWriter; consumed by CalorimeterWriter, which attaches each
  // shower's calorimeter hits to the cluster built from the same shower.
  std::optional<emca::Output> emca;

  // Set by TrackingWriter; consumed by VertexWriter, CalorimeterWriter and
  // ParticleIdWriter, and by particleForPa() in pass 1.
  std::optional<tracking::Output> tracking;

  // Pass-2 only: per-fDST-PA index -> sDST_TRAC_Tracks index, or -1
  // if no perigee-match. Built by MatchProvenanceWriter from the
  // fDST PA-chain walk; consumed by TrackHybridWriter to find the
  // sDST Track to clone and extend.
  std::optional<std::vector<int>> fdst_pa_to_sdst_track;

  // Pass-2 only: per-fDST-PA index -> sDST_MAIN_Particles index, or
  // -1 if no match / unmatched. Built by MatchProvenanceWriter
  // alongside fdst_pa_to_sdst_track; consumed through particleForPa() by
  // the writers that link a ParticleID to its particle.
  std::optional<std::vector<int>> fdst_pa_to_sdst_particle;
};

// Where a collection's values came from.
//
//   Transcribed  the stored DST value, read from a ZEBRA bank word through
//                PHDST. Unit conversion and basis change still count as
//                transcription: the same measurement, represented differently.
//   Derived      reconstructed at conversion time from stored banks or by a
//                DELPHI service — a refit, re-clustering, or recomputed tag.
//   Custom       computed by this converter. Not a DELPHI quantity at all.
//
// Every put() states one. A collection that would mix them must be split.
enum class Provenance { Transcribed, Derived, Custom };

// Record a collection's provenance. Called by put() and putParameter().
void noteProvenance(std::string_view name, Provenance prov);

// Print the provenance summary. Call once at end of job.
void reportProvenance();

// The recorded provenance, as two parallel vectors: collection names and
// their source. Written once per job into the metadata frame.
struct ProvenanceRecord {
  std::vector<std::string> collections;
  std::vector<std::string> sources;
};
ProvenanceRecord provenanceRecord();

class CollectionWriter {
public:
  CollectionWriter(podio::Frame& frame,
                   EventContext& ctx,
                   bank::Pass pass)
    : frame_(frame), ctx_(ctx), pass_(pass) {}

  virtual ~CollectionWriter() = default;

  // Domain-specific implementation point.
  virtual void emit() = 0;

protected:
  podio::Frame&    frame_;
  EventContext&    ctx_;
  bank::Pass       pass_;

  // True when this writer is running over a fullDST. Writers that decode a
  // bank differently in the two passes branch on this rather than on the
  // collection prefix, which varies per bank.
  bool fromFullDst() const { return pass_ == bank::Pass::Fdst; }

  // The reconstructed particle this PA belongs to, or empty if it has none.
  //
  // `paIdx` is the PA's position in the event's PA-chain walk
  // (pawalk::forEachPA) -- the index every writer shares.
  //
  // The two passes establish the mapping differently. Pass 1 knows it
  // directly: TrackingWriter created the particles from this same walk, so the
  // index maps straight through. Pass 2 reads a fullDST, whose PA chain is a
  // different set of tracks, and recovers the correspondence by matching
  // perigees onto the pass-1 tracks -- a match that can fail.
  //
  // A writer that runs in both passes asks here rather than choosing a map
  // itself, so it never has to know which pass it is in.
  std::optional<edm4hep::ReconstructedParticle> particleForPa(int paIdx) const;

  // Build a canonical collection / parameter name
  // "<prefix>_<bank>_<readable>". Also the way to name a collection written
  // by another writer, since the prefix follows the bank.
  std::string makeName(std::string_view bank,
                       std::string_view readable) const {
    return bank::make(pass_, bank, readable);
  }

  // Name of a collection written by pass 1. Pass-2 writers read pass-1 output,
  // whose prefix follows the bank rather than the pass running now.
  std::string sdstName(std::string_view bank,
                       std::string_view readable) const {
    return bank::make(bank::Pass::Sdst, bank, readable);
  }

  // Move a freshly-built collection into the frame under
  // "<source>_<bank>_<readable>". `prov` states where the values came from;
  // see Provenance. Returns the const reference podio returns, for callers
  // that need to cross-reference the in-frame copy.
  template <typename Coll>
  const Coll& put(Coll&& coll,
                  std::string_view bank,
                  std::string_view readable,
                  Provenance prov) {
    auto name = makeName(bank, readable);
    noteProvenance(name, prov);
    return frame_.put(std::move(coll), std::move(name));
  }


  // Frame-parameter equivalent. Name is "<source>_<bank>_<key>".
  // Type T must be one of podio's supported parameter types
  // (int, float, double, std::string, std::vector<...> of those, ...).
  template <typename T>
  void putParameter(std::string_view bank,
                    std::string_view key,
                    T&& value,
                    Provenance prov) {
    auto name = makeName(bank, key);
    noteProvenance(name, prov);
    frame_.putParameter(std::move(name), std::forward<T>(value));
  }


  // A run of parameters sharing one bank tag and one provenance, so the
  // provenance is stated once for the group rather than at every parameter:
  //
  //   auto stored = parameters("EVT", Provenance::Transcribed);
  //   stored("runNumber", ph::IIIRUN);
  //   stored("date",      ph::IIIDAT);
  class ParameterGroup {
  public:
    ParameterGroup(CollectionWriter& writer,
                   std::string_view bank,
                   Provenance prov)
      : writer_(writer), bank_(bank), prov_(prov) {}

    template <typename T>
    void operator()(std::string_view key, T&& value) const {
      writer_.putParameter(bank_, key, std::forward<T>(value), prov_);
    }

  private:
    CollectionWriter& writer_;
    std::string_view  bank_;
    Provenance        prov_;
  };

  ParameterGroup parameters(std::string_view bank, Provenance prov) {
    return ParameterGroup(*this, bank, prov);
  }
};

}  // namespace delphi_edm4hep
