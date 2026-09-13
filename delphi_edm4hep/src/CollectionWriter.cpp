// Provenance registry.
//
// Collects the source of every collection emitted during a job and prints a
// summary at the end. The summary flags derived collections whose name carries
// a DST bank mnemonic, since that naming implies a transcription.

#include "delphi_edm4hep/CollectionWriter.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>

namespace delphi_edm4hep {

namespace {

// Collection name -> provenance, first writer wins. Collections are emitted
// once per event, so this deduplicates across the job.
std::map<std::string, Provenance, std::less<>> g_seen;

// PA extra-module (blocklet) names accepted by PHDST's LPHPA, plus the
// event-level bank mnemonics used in collection names. A name built from one
// of these asserts that its values are stored DST content.
constexpr std::string_view kBankMnemonics[] = {
  "BSP",  "EL",   "ELID", "ELTR", "EMCA", "EMNC", "HAID", "HCAL",
  "HCMU", "HCNC", "HCRO", "LUJ",  "MAIN", "MRIC", "MTPC", "MU",   "MUFI",
  "MUID", "ODHI", "PHC",  "PHOT", "PXTD", "SSTC", "STIC", "TBL",  "TDHA",
  "TDID",
  "TDVD", "TEAD", "TEFA", "TEFB", "TEID", "TEOD", "TERB", "TERF", "TEST",
  "TETP", "TEVF", "TOF",  "TRAC", "TRAX", "V0",
};

// Extract the "<bank>" field of "<source>_<bank>_<readable>".
std::string_view bankField(std::string_view name) {
  const auto first = name.find('_');
  if (first == std::string_view::npos) return {};
  const auto second = name.find('_', first + 1);
  if (second == std::string_view::npos) return {};
  return name.substr(first + 1, second - first - 1);
}

bool isBankMnemonic(std::string_view bank) {
  return std::find(std::begin(kBankMnemonics), std::end(kBankMnemonics), bank)
         != std::end(kBankMnemonics);
}

}  // namespace

std::optional<edm4hep::ReconstructedParticle>
CollectionWriter::particleForPa(int paIdx) const {
  // Pass 1. TrackingWriter set ctx_.tracking earlier in this event, and its
  // pa_to_particle is indexed by the same PA-walk position.
  if (ctx_.tracking) {
    const auto& t = *ctx_.tracking;
    // The map stops at the last PA Tracking looked at, so a higher index is
    // simply absent rather than an error.
    if (paIdx < 0 || paIdx >= static_cast<int>(t.pa_to_particle.size())) {
      return std::nullopt;
    }
    // -1 marks a PA that yielded no particle -- Tracking skips those it cannot
    // give a momentum.
    const int i = t.pa_to_particle[paIdx];
    if (i < 0) return std::nullopt;
    return t.particle_handles[i];
  }

  // Pass 2. ctx_.tracking is unset here because TrackingWriter does not run;
  // MatchProvenanceWriter supplies the perigee match instead, whose entries
  // index the fDST particles cloned from pass 1.
  if (ctx_.fdst_pa_to_sdst_particle) {
    const auto& map = *ctx_.fdst_pa_to_sdst_particle;
    if (paIdx < 0 || paIdx >= static_cast<int>(map.size())) return std::nullopt;
    // -1 marks a fullDST PA whose perigee matched no pass-1 track.
    const int i = map[paIdx];
    if (i < 0) return std::nullopt;
    const auto& particles =
      frame_.get<edm4hep::ReconstructedParticleCollection>(
        makeName("MAIN", "Particles"));
    // Guard the clone being shorter than the map: better to return nothing
    // than to link the wrong particle.
    if (i >= static_cast<int>(particles.size())) return std::nullopt;
    return particles[i];
  }

  // Neither map is present -- the writer is running before Tracking or
  // MatchProvenance, or on an event where they produced nothing.
  return std::nullopt;
}

const char* label(Provenance prov) {
  switch (prov) {
    case Provenance::Derived: return "derived";
    case Provenance::Custom:  return "custom";
    default:                  return "transcribed";
  }
}

void noteProvenance(std::string_view name, Provenance prov) {
  g_seen.emplace(std::string(name), prov);
}

ProvenanceRecord provenanceRecord() {
  ProvenanceRecord record;
  record.collections.reserve(g_seen.size());
  record.sources.reserve(g_seen.size());
  for (const auto& [name, prov] : g_seen) {
    record.collections.push_back(name);
    record.sources.emplace_back(label(prov));
  }
  return record;
}

void reportProvenance() {
  if (g_seen.empty()) return;

  std::size_t transcribed = 0, derived = 0, custom = 0;
  std::cout << "delphi_edm4hep: provenance summary\n";
  for (const auto& [name, prov] : g_seen) {
    switch (prov) {
      case Provenance::Transcribed: ++transcribed; break;
      case Provenance::Derived:     ++derived;     break;
      case Provenance::Custom:      ++custom;      break;
    }
    // A bank mnemonic asserts stored DST content, so anything not
    // transcribed is wearing a name it has not earned.
    const bool mismatch = prov != Provenance::Transcribed
                          && isBankMnemonic(bankField(name));
    std::printf("  %-11s  %s%s\n", label(prov), name.c_str(),
                mismatch ? "   [bank mnemonic]" : "");
  }
  std::cout << "  " << transcribed << " transcribed, " << derived
            << " derived, " << custom << " custom\n";
}

}  // namespace delphi_edm4hep
