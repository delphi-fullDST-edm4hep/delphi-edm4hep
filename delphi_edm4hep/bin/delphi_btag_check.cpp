// delphi_btag_check — integrity/provenance check for prefix-isolated b-tag
// conversions.
//
// Scan every event before a batch driver publishes a converted file. The final
// line is stable key=value output suitable for campaign audit scripts.

#include "delphi_edm4hep/internal/BtagCheckDomains.h"
#include "delphi_edm4hep/internal/BtagProvenance.h"

#include <edm4hep/ReconstructedParticleCollection.h>
#include <edm4hep/VertexCollection.h>
#include <podio/Frame.h>
#include <podio/ROOTReader.h>
#include <podio/UserDataCollection.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

namespace domain = delphi_edm4hep::btag::check;

struct Stats {
  std::uint64_t entries = 0;
  std::uint64_t framesChecked = 0;
  std::uint64_t frameReadFailures = 0;
  std::uint64_t runFailures = 0;
  std::uint64_t referenceEntryCountFailures = 0;
  std::uint64_t referenceFrameReadFailures = 0;
  std::uint64_t referenceSourceLocalProvenanceFrames = 0;
  std::uint64_t legacyReferenceFallbackFrames = 0;
  std::uint64_t identityFailures = 0;
  std::uint64_t modeFailures = 0;
  std::uint64_t recalculatedFailures = 0;
  std::uint64_t provenanceFailures = 0;
  std::uint64_t primaryVertexPolicyFailures = 0;
  std::uint64_t sourceLocalProvenanceFrames = 0;
  std::uint64_t legacySdstProvenanceFrames = 0;
  std::uint64_t legacyFdstFallbackFrames = 0;
  std::uint64_t keepDelanaPolicyFrames = 0;
  std::uint64_t replaceWithAabtagPolicyFrames = 0;
  std::uint64_t iflpvtZeroFrames = 0;
  std::uint64_t iflpvtOneFrames = 0;
  std::uint64_t missingEventParameters = 0;
  std::uint64_t offPayloadPresenceFailures = 0;
  std::uint64_t bankPayloadEmptyFailures = 0;
  std::uint64_t eventVectorLengthFailures = 0;
  std::uint64_t eventProbabilityNans = 0;
  std::uint64_t eventProbabilityRangeFailures = 0;
  std::uint64_t thrustNans = 0;
  std::uint64_t thrustRangeFailures = 0;
  std::uint64_t missingTrackParameters = 0;
  std::uint64_t missingCollections = 0;
  std::uint64_t trackVectorLengthFailures = 0;
  std::uint64_t trackProbabilityNonfiniteFailures = 0;
  std::uint64_t trackProbabilityRangeFailures = 0;
  std::uint64_t trackImpactNonfiniteFailures = 0;
  std::uint64_t trackErrorDomainFailures = 0;
  std::uint64_t trackChi2DomainFailures = 0;
  std::uint64_t trackMomentumDomainFailures = 0;
  std::uint64_t trackIntegerDomainFailures = 0;
  std::uint64_t pvSizeFailures = 0;
  std::uint64_t pvPrimaryFlagFailures = 0;
  std::uint64_t pvAlgorithmTypeFailures = 0;
  std::uint64_t pvNonfiniteFailures = 0;
  std::uint64_t pvNdfDomainFailures = 0;
  std::uint64_t tracks = 0;
  std::uint64_t unresolvedParticleIndices = 0;
  std::uint64_t particleIndexRangeFailures = 0;
  std::uint64_t truncatedEvents = 0;
  std::uint64_t badStatusEvents = 0;
  std::uint64_t beamspotErrorEvents = 0;
  std::uint64_t aabtagNotInvokedEvents = 0;
  std::uint64_t aabtagInvalidEvents = 0;
  std::uint64_t validityContractFailures = 0;
  std::uint64_t truncationFailures = 0;
  std::uint64_t attachedConsistencyFailures = 0;

  std::uint64_t failures() const {
    return frameReadFailures + runFailures + referenceEntryCountFailures +
           referenceFrameReadFailures + identityFailures + modeFailures +
           recalculatedFailures + provenanceFailures +
           primaryVertexPolicyFailures + missingEventParameters +
           offPayloadPresenceFailures + bankPayloadEmptyFailures +
           eventVectorLengthFailures + eventProbabilityRangeFailures +
           thrustRangeFailures + missingTrackParameters + missingCollections +
           trackVectorLengthFailures + trackProbabilityNonfiniteFailures +
           trackProbabilityRangeFailures + trackImpactNonfiniteFailures +
           trackErrorDomainFailures + trackChi2DomainFailures +
           trackMomentumDomainFailures + trackIntegerDomainFailures +
           pvSizeFailures + pvPrimaryFlagFailures +
           pvAlgorithmTypeFailures + pvNonfiniteFailures +
           pvNdfDomainFailures +
           particleIndexRangeFailures + truncationFailures +
           attachedConsistencyFailures + validityContractFailures;
  }
};

struct Options {
  std::string input;
  std::string expectedRun;
  std::string mode;
  std::string identityReference;
  std::string source = "sDST";
  std::string expectedPrimaryVertexPolicy;
  bool legacyFdstBeamspotFallback = false;
  bool legacyReferenceBeamspotFallback = false;
};

void usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " [--source sDST|fDST]"
               " [--primary-vertex-policy keep-delana|replace-with-aabtag]"
               " [--legacy-fdst-beamspot-fallback]"
               " [--legacy-reference-beamspot-fallback]"
               " <input.edm4hep.root> <expected-negative-run|data>"
               " <off|bank|recalc> [identity-reference.edm4hep.root]\n";
}

bool parseOptions(int argc, char** argv, Options& options) {
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--source") {
      if (++i >= argc) {
        std::cerr << "error: --source requires sDST or fDST\n";
        return false;
      }
      options.source = argv[i];
    } else if (arg.starts_with("--source=")) {
      options.source = std::string(arg.substr(std::string_view("--source=").size()));
    } else if (arg == "--primary-vertex-policy") {
      if (++i >= argc) {
        std::cerr << "error: --primary-vertex-policy requires "
                     "keep-delana or replace-with-aabtag\n";
        return false;
      }
      options.expectedPrimaryVertexPolicy = argv[i];
    } else if (arg.starts_with("--primary-vertex-policy=")) {
      options.expectedPrimaryVertexPolicy = std::string(
          arg.substr(std::string_view("--primary-vertex-policy=").size()));
    } else if (arg == "--legacy-fdst-beamspot-fallback") {
      options.legacyFdstBeamspotFallback = true;
    } else if (arg == "--legacy-reference-beamspot-fallback") {
      options.legacyReferenceBeamspotFallback = true;
    } else if (arg.starts_with("--")) {
      std::cerr << "error: unknown option: " << arg << "\n";
      return false;
    } else {
      positional.emplace_back(arg);
    }
  }

  if (positional.size() != 3 && positional.size() != 4) return false;
  options.input = positional[0];
  options.expectedRun = positional[1];
  options.mode = positional[2];
  if (positional.size() == 4) options.identityReference = positional[3];

  if (options.source != "sDST" && options.source != "fDST") {
    std::cerr << "error: --source must be sDST or fDST, got: "
              << options.source << "\n";
    return false;
  }
  if (!options.expectedPrimaryVertexPolicy.empty() &&
      options.expectedPrimaryVertexPolicy !=
          delphi_edm4hep::btag::provenance::kKeepDelana &&
      options.expectedPrimaryVertexPolicy !=
          delphi_edm4hep::btag::provenance::kReplaceWithAabtag) {
    std::cerr << "error: --primary-vertex-policy must be keep-delana or "
                 "replace-with-aabtag, got: "
              << options.expectedPrimaryVertexPolicy << "\n";
    return false;
  }
  if (options.legacyFdstBeamspotFallback && options.source != "fDST") {
    std::cerr << "error: --legacy-fdst-beamspot-fallback is valid only with "
                 "--source fDST\n";
    return false;
  }
  if (options.source == "fDST" && !options.identityReference.empty()) {
    std::cerr << "error: an identity reference validates copied sDST_EVT "
                 "identity, not fDST content; run a separate --source sDST "
                 "check for identity\n";
    return false;
  }
  if (options.legacyReferenceBeamspotFallback &&
      options.identityReference.empty()) {
    std::cerr << "error: --legacy-reference-beamspot-fallback requires an "
                 "identity-reference file\n";
    return false;
  }
  return true;
}

bool parseInt(std::string_view text, int& value) {
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool hasCollection(const std::unordered_set<std::string>& names,
                   const std::string& name, Stats& stats) {
  if (names.contains(name)) return true;
  ++stats.missingCollections;
  return false;
}

void checkProbability(float value, bool allowNan, std::uint64_t& nanCounter,
                      std::uint64_t& nonfiniteCounter,
                      std::uint64_t& rangeCounter) {
  if (std::isnan(value)) {
    ++nanCounter;
    if (!allowNan) ++nonfiniteCounter;
  } else if (!std::isfinite(value)) {
    ++nonfiniteCounter;
  } else if (value < 0.f || value > 1.000001f) {
    ++rangeCounter;
  }
}

std::string provenanceSchema(const Stats& s) {
  const auto classified = s.sourceLocalProvenanceFrames +
                          s.legacySdstProvenanceFrames +
                          s.legacyFdstFallbackFrames;
  if (s.framesChecked == 0 || classified != s.framesChecked) return "missing";
  if (s.sourceLocalProvenanceFrames == s.framesChecked) return "source-local-v1";
  if (s.legacySdstProvenanceFrames == s.framesChecked) return "legacy-sdst-evt";
  if (s.legacyFdstFallbackFrames == s.framesChecked) {
    return "legacy-missing-source-local-btagcfg";
  }
  return "mixed";
}

std::string beamspotStatusSource(const Stats& s, const Options& options) {
  if (s.framesChecked != 0 &&
      s.sourceLocalProvenanceFrames == s.framesChecked) {
    return options.source + "_BTAGCFG_BeamSpotErrorCode";
  }
  if (s.framesChecked != 0 &&
      (s.legacySdstProvenanceFrames == s.framesChecked ||
       s.legacyFdstFallbackFrames == s.framesChecked)) {
    return "sDST_EVT_BeamSpotErrorCode";
  }
  const auto classified = s.sourceLocalProvenanceFrames +
                          s.legacySdstProvenanceFrames +
                          s.legacyFdstFallbackFrames;
  return classified == 0 ? "missing" : "mixed";
}

std::string primaryVertexPolicySummary(const Stats& s) {
  if (s.framesChecked == 0) return "missing";
  if (s.sourceLocalProvenanceFrames == s.framesChecked) {
    if (s.keepDelanaPolicyFrames == s.framesChecked) return "keep-delana";
    if (s.replaceWithAabtagPolicyFrames == s.framesChecked) {
      return "replace-with-aabtag";
    }
    return "mixed-or-invalid";
  }
  if (s.legacySdstProvenanceFrames + s.legacyFdstFallbackFrames ==
      s.framesChecked) {
    return "legacy-unrecorded";
  }
  return "missing-or-mixed";
}

std::string iflpvtSummary(const Stats& s) {
  if (s.framesChecked == 0) return "missing";
  if (s.sourceLocalProvenanceFrames == s.framesChecked) {
    if (s.iflpvtZeroFrames == s.framesChecked) return "0";
    if (s.iflpvtOneFrames == s.framesChecked) return "1";
    return "mixed-or-invalid";
  }
  if (s.legacySdstProvenanceFrames + s.legacyFdstFallbackFrames ==
      s.framesChecked) {
    return "legacy-unrecorded";
  }
  return "missing-or-mixed";
}

std::string referenceProvenanceSchema(const Stats& s, const Options& options) {
  if (options.identityReference.empty()) return "not-applicable";
  const auto classified = s.referenceSourceLocalProvenanceFrames +
                          s.legacyReferenceFallbackFrames;
  if (classified != s.framesChecked) return "missing-or-partial";
  if (s.referenceSourceLocalProvenanceFrames == s.framesChecked) {
    return "source-local-v1";
  }
  if (s.legacyReferenceFallbackFrames == s.framesChecked) {
    return "legacy-missing-source-local-btagcfg";
  }
  return "mixed";
}

void printResult(const Stats& s, const Options& options,
                 int expectedRecalculated,
                 std::uint64_t referenceEntries) {
  std::cout
      << "status=" << (s.failures() == 0 ? "PASS" : "FAIL")
      << " entries=" << s.entries
      << " expected_run=" << options.expectedRun
      << " source=" << options.source
      << " identity_source=sDST_EVT"
      << " btag_mode=" << options.mode
      << " recalculated_expected=" << expectedRecalculated
      << " provenance_schema=" << provenanceSchema(s)
      << " beamspot_status_source=" << beamspotStatusSource(s, options)
      << " primary_vertex_policy=" << primaryVertexPolicySummary(s)
      << " primary_vertex_policy_expected="
      << (options.expectedPrimaryVertexPolicy.empty()
              ? "not-required"
              : options.expectedPrimaryVertexPolicy)
      << " iflpvt=" << iflpvtSummary(s)
      << " legacy_fdst_beamspot_fallback_requested="
      << (options.legacyFdstBeamspotFallback ? 1 : 0)
      << " legacy_fdst_beamspot_fallback_used="
      << (s.legacyFdstFallbackFrames != 0 ? 1 : 0)
      << " frames_checked=" << s.framesChecked
      << " failures=" << s.failures()
      << " frame_read_failures=" << s.frameReadFailures
      << " run_failures=" << s.runFailures
      << " identity_reference="
      << std::quoted(options.identityReference.empty()
                         ? std::string("none")
                         : options.identityReference)
      << " reference_provenance_schema="
      << referenceProvenanceSchema(s, options)
      << " legacy_reference_beamspot_fallback_requested="
      << (options.legacyReferenceBeamspotFallback ? 1 : 0)
      << " legacy_reference_beamspot_fallback_used="
      << (s.legacyReferenceFallbackFrames != 0 ? 1 : 0)
      << " reference_entries=" << referenceEntries
      << " reference_entry_count_failures="
      << s.referenceEntryCountFailures
      << " reference_frame_read_failures=" << s.referenceFrameReadFailures
      << " reference_source_local_provenance_frames="
      << s.referenceSourceLocalProvenanceFrames
      << " legacy_reference_fallback_frames="
      << s.legacyReferenceFallbackFrames
      << " identity_failures=" << s.identityFailures
      << " mode_failures=" << s.modeFailures
      << " recalculated_failures=" << s.recalculatedFailures
      << " provenance_failures=" << s.provenanceFailures
      << " primary_vertex_policy_failures="
      << s.primaryVertexPolicyFailures
      << " source_local_provenance_frames="
      << s.sourceLocalProvenanceFrames
      << " legacy_sdst_provenance_frames="
      << s.legacySdstProvenanceFrames
      << " legacy_fdst_fallback_frames="
      << s.legacyFdstFallbackFrames
      << " missing_event_parameters=" << s.missingEventParameters
      << " off_payload_presence_failures="
      << s.offPayloadPresenceFailures
      << " bank_payload_empty_failures=" << s.bankPayloadEmptyFailures
      << " event_vector_length_failures=" << s.eventVectorLengthFailures
      << " event_probability_nans=" << s.eventProbabilityNans
      << " event_probability_range_failures=" << s.eventProbabilityRangeFailures
      << " thrust_nans=" << s.thrustNans
      << " thrust_range_failures=" << s.thrustRangeFailures
      << " missing_track_parameters=" << s.missingTrackParameters
      << " missing_collections=" << s.missingCollections
      << " track_vector_length_failures=" << s.trackVectorLengthFailures
      << " track_probability_nonfinite_failures="
      << s.trackProbabilityNonfiniteFailures
      << " track_probability_range_failures=" << s.trackProbabilityRangeFailures
      << " track_impact_nonfinite_failures="
      << s.trackImpactNonfiniteFailures
      << " track_error_domain_failures=" << s.trackErrorDomainFailures
      << " track_chi2_domain_failures=" << s.trackChi2DomainFailures
      << " track_momentum_domain_failures=" << s.trackMomentumDomainFailures
      << " track_integer_domain_failures=" << s.trackIntegerDomainFailures
      << " pv_size_failures=" << s.pvSizeFailures
      << " pv_primary_flag_failures=" << s.pvPrimaryFlagFailures
      << " pv_algorithm_type_failures=" << s.pvAlgorithmTypeFailures
      << " pv_nonfinite_failures=" << s.pvNonfiniteFailures
      << " pv_ndf_domain_failures=" << s.pvNdfDomainFailures
      << " tracks=" << s.tracks
      << " unresolved_particle_indices=" << s.unresolvedParticleIndices
      << " particle_index_range_failures=" << s.particleIndexRangeFailures
      << " truncated_events=" << s.truncatedEvents
      << " bad_status_events=" << s.badStatusEvents
      << " beamspot_error_events=" << s.beamspotErrorEvents
      << " aabtag_not_invoked_events=" << s.aabtagNotInvokedEvents
      << " aabtag_invalid_events=" << s.aabtagInvalidEvents
      << " validity_contract_failures=" << s.validityContractFailures
      << " truncation_failures=" << s.truncationFailures
      << " attached_consistency_failures=" << s.attachedConsistencyFailures
      << '\n';
}

template <typename T>
const podio::UserDataCollection<T>* getUserData(
    const podio::Frame& frame, const std::unordered_set<std::string>& names,
    const std::string& name, Stats& stats) {
  if (!hasCollection(names, name, stats)) return nullptr;
  return &frame.get<podio::UserDataCollection<T>>(name);
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parseOptions(argc, argv, options)) {
    usage(argv[0]);
    return 1;
  }

  const std::string& expectedRunText = options.expectedRun;
  const bool expectData = expectedRunText == "data";
  int expectedRun = 0;
  if (!expectData &&
      (!parseInt(expectedRunText, expectedRun) || expectedRun >= 0)) {
    std::cerr << "error: expected-run must be 'data' or a negative MC run: "
              << expectedRunText << "\n";
    usage(argv[0]);
    return 1;
  }

  const std::string& mode = options.mode;
  if (mode != "off" && mode != "bank" && mode != "recalc") {
    std::cerr << "error: mode must be off, bank, or recalc, got: " << mode
              << "\n";
    usage(argv[0]);
    return 1;
  }
  // SKELANA always recalculates on a fullDST when IFLBTG > 0, including the
  // legacy `bank` steering value. Name and validate the payload for what the
  // selected pass actually did, not just for the requested steering word.
  const bool expectedPayloadRecalculated =
      mode == "recalc" || (options.source == "fDST" && mode == "bank");
  const int expectedRecalculated = expectedPayloadRecalculated ? 1 : 0;
  const std::string bank = expectedPayloadRecalculated ? "AABTAG" : "BTG";
  const std::string configPrefix = options.source + "_BTAGCFG_";
  const std::string valuePrefix = options.source + "_" + bank + "_";
  const std::string& identityReference = options.identityReference;
  Stats stats;
  std::uint64_t referenceEntries = 0;

  try {
    podio::ROOTReader reader;
    reader.openFile(options.input);
    stats.entries = reader.getEntries("events");
    if (stats.entries == 0) {
      ++stats.frameReadFailures;
      printResult(stats, options, expectedRecalculated, referenceEntries);
      return 2;
    }

    podio::ROOTReader referenceReader;
    if (!identityReference.empty()) {
      referenceReader.openFile(identityReference);
      referenceEntries = referenceReader.getEntries("events");
      if (referenceEntries != stats.entries) {
        ++stats.referenceEntryCountFailures;
      }
    }

    for (unsigned entry = 0; entry < stats.entries; ++entry) {
      auto data = reader.readEntry("events", entry);
      if (!data) {
        ++stats.frameReadFailures;
        continue;
      }
      const podio::Frame frame(std::move(data));
      ++stats.framesChecked;

      const auto run = frame.getParameter<int>("sDST_EVT_runNumber");
      const auto event = frame.getParameter<int>("sDST_EVT_eventNumber");
      const auto fileSeq = frame.getParameter<int>("sDST_EVT_fileSeq");
      if (!run || (expectData ? *run <= 0 : *run != expectedRun)) {
        ++stats.runFailures;
      }
      if (!run || !event || !fileSeq) ++stats.identityFailures;

      if (!identityReference.empty()) {
        if (entry >= referenceEntries) {
          ++stats.identityFailures;
        } else {
          auto referenceData = referenceReader.readEntry("events", entry);
          if (!referenceData) {
            ++stats.referenceFrameReadFailures;
          } else {
            const podio::Frame referenceFrame(std::move(referenceData));
            const auto referenceRun =
                referenceFrame.getParameter<int>("sDST_EVT_runNumber");
            const auto referenceEvent =
                referenceFrame.getParameter<int>("sDST_EVT_eventNumber");
            const auto referenceFileSeq =
                referenceFrame.getParameter<int>("sDST_EVT_fileSeq");
            if (run && event && fileSeq &&
                (!referenceRun || !referenceEvent || !referenceFileSeq ||
                 *run != *referenceRun || *event != *referenceEvent ||
                 *fileSeq != *referenceFileSeq)) {
              ++stats.identityFailures;
            }
            // The optional identity reference is the recalculated leg used by
            // the stored-bank closure campaign. Prove that provenance from
            // the frames themselves; a path and a touch-style marker are not
            // evidence that the reference actually contains AABTAG output.
            const auto referenceMode = referenceFrame.getParameter<std::string>(
                "sDST_BTAGCFG_Mode");
            const auto referenceRecalculated =
                referenceFrame.getParameter<int>(
                    "sDST_BTAGCFG_Recalculated");
            if (!referenceMode || *referenceMode != "recalc" ||
                !referenceRecalculated || *referenceRecalculated != 1) {
              ++stats.identityFailures;
            }
            auto referenceBeamSpotError = referenceFrame.getParameter<int>(
                "sDST_BTAGCFG_BeamSpotErrorCode");
            const auto referenceSourcePrefix =
                referenceFrame.getParameter<std::string>(
                    "sDST_BTAGCFG_SourcePrefix");
            const auto referencePrimaryVertexPolicy =
                referenceFrame.getParameter<std::string>(
                    "sDST_BTAGCFG_PrimaryVertexPolicy");
            const auto referenceIflpvt = referenceFrame.getParameter<int>(
                "sDST_BTAGCFG_IFLPVT");
            if (referenceBeamSpotError) {
              ++stats.referenceSourceLocalProvenanceFrames;
              if (!referenceSourcePrefix || *referenceSourcePrefix != "sDST" ||
                  !referencePrimaryVertexPolicy || !referenceIflpvt ||
                  (*referenceIflpvt != 0 && *referenceIflpvt != 1) ||
                  *referencePrimaryVertexPolicy !=
                      delphi_edm4hep::btag::provenance::primaryVertexPolicy(
                          *referenceIflpvt)) {
                ++stats.identityFailures;
              }
            } else {
              if (referenceSourcePrefix || referencePrimaryVertexPolicy ||
                  referenceIflpvt) {
                ++stats.identityFailures;
              }
              if (options.legacyReferenceBeamspotFallback) {
                referenceBeamSpotError = referenceFrame.getParameter<int>(
                    "sDST_EVT_BeamSpotErrorCode");
                if (referenceBeamSpotError) {
                  ++stats.legacyReferenceFallbackFrames;
                }
              }
            }
            const auto referenceBadEventCode =
                referenceFrame.getParameter<int>(
                    "sDST_AABTAG_BadEventCode");
            const auto referenceAlgorithmInvoked =
                referenceFrame.getParameter<int>(
                    "sDST_AABTAG_AlgorithmInvoked");
            const auto referenceValid =
                referenceFrame.getParameter<int>("sDST_AABTAG_Valid");
            if (!referenceBeamSpotError || !referenceBadEventCode ||
                !referenceAlgorithmInvoked || !referenceValid ||
                *referenceBadEventCode < 0 || *referenceBadEventCode > 2 ||
                (*referenceAlgorithmInvoked != 0 &&
                 *referenceAlgorithmInvoked != 1) ||
                (*referenceValid != 0 && *referenceValid != 1)) {
              ++stats.identityFailures;
            } else {
              const int expectedReferenceInvoked =
                  *referenceBeamSpotError == 0 ? 1 : 0;
              const int expectedReferenceValid =
                  expectedReferenceInvoked == 1 &&
                          *referenceBadEventCode == 0
                      ? 1
                      : 0;
              if (*referenceAlgorithmInvoked != expectedReferenceInvoked ||
                  *referenceValid != expectedReferenceValid) {
                ++stats.identityFailures;
              }
            }
          }
        }
      }

      const auto actualMode =
          frame.getParameter<std::string>(configPrefix + "Mode");
      if (!actualMode || *actualMode != mode) ++stats.modeFailures;

      const auto recalculated =
          frame.getParameter<int>(configPrefix + "Recalculated");
      if (!recalculated || *recalculated != expectedRecalculated) {
        ++stats.recalculatedFailures;
      }

      bool validityContractFailure = false;
      bool provenanceFailure = false;
      std::optional<int> beamSpotErrorCode =
          frame.getParameter<int>(configPrefix + "BeamSpotErrorCode");
      const auto sourcePrefix =
          frame.getParameter<std::string>(configPrefix + "SourcePrefix");
      const auto primaryVertexPolicy = frame.getParameter<std::string>(
          configPrefix + "PrimaryVertexPolicy");
      const auto iflpvt = frame.getParameter<int>(configPrefix + "IFLPVT");
      if (beamSpotErrorCode) {
        ++stats.sourceLocalProvenanceFrames;
        if (!sourcePrefix || *sourcePrefix != options.source || !iflpvt ||
            (*iflpvt != 0 && *iflpvt != 1) || !primaryVertexPolicy) {
          provenanceFailure = true;
        } else {
          const auto expectedPolicy =
              delphi_edm4hep::btag::provenance::primaryVertexPolicy(*iflpvt);
          if (*primaryVertexPolicy != expectedPolicy) provenanceFailure = true;
          if (*primaryVertexPolicy ==
              delphi_edm4hep::btag::provenance::kKeepDelana) {
            ++stats.keepDelanaPolicyFrames;
          } else if (*primaryVertexPolicy ==
                     delphi_edm4hep::btag::provenance::kReplaceWithAabtag) {
            ++stats.replaceWithAabtagPolicyFrames;
          }
          if (*iflpvt == 0) {
            ++stats.iflpvtZeroFrames;
          } else if (*iflpvt == 1) {
            ++stats.iflpvtOneFrames;
          }
        }
      } else if (options.source == "sDST") {
        // Pre-prefix-isolation sDST files carried the same current-pass value
        // under EVT. Preserve read compatibility while making the schema
        // downgrade visible in the stable summary.
        beamSpotErrorCode =
            frame.getParameter<int>("sDST_EVT_BeamSpotErrorCode");
        if (sourcePrefix || primaryVertexPolicy || iflpvt) {
          provenanceFailure = true;
        }
        if (beamSpotErrorCode) {
          ++stats.legacySdstProvenanceFrames;
        } else {
          provenanceFailure = true;
        }
      } else if (options.legacyFdstBeamspotFallback) {
        // This is deliberately opt-in: copied sDST_EVT metadata is not proof
        // of the beamspot status that governed the live fullDST pass. It is
        // only a migration path for already-frozen pass-2 outputs.
        beamSpotErrorCode =
            frame.getParameter<int>("sDST_EVT_BeamSpotErrorCode");
        if (sourcePrefix || primaryVertexPolicy || iflpvt) {
          provenanceFailure = true;
        }
        if (beamSpotErrorCode) {
          ++stats.legacyFdstFallbackFrames;
        } else {
          provenanceFailure = true;
        }
      } else {
        provenanceFailure = true;
      }
      if (provenanceFailure) ++stats.provenanceFailures;
      if (!options.expectedPrimaryVertexPolicy.empty()) {
        const int expectedIflpvt =
            options.expectedPrimaryVertexPolicy ==
                    delphi_edm4hep::btag::provenance::kKeepDelana
                ? 0
                : 1;
        if (!primaryVertexPolicy ||
            *primaryVertexPolicy != options.expectedPrimaryVertexPolicy ||
            !iflpvt || *iflpvt != expectedIflpvt) {
          ++stats.primaryVertexPolicyFailures;
        }
      }
      if (!beamSpotErrorCode) {
        ++stats.missingEventParameters;
      } else if (*beamSpotErrorCode != 0) {
        ++stats.beamspotErrorEvents;
      }

      const auto namesVector = frame.getAvailableCollections();
      const std::unordered_set<std::string> names(namesVector.begin(),
                                                   namesVector.end());

      if (mode == "off") {
        const auto containsSelectedPayload = [&](const auto& candidateNames) {
          for (const auto& name : candidateNames) {
            if (domain::isSelectedPayloadName(name, options.source)) {
              return true;
            }
          }
          return false;
        };
        // Off still emits BTAGCFG provenance. It must not retain either rich
        // AABTAG content or the legacy stored-bank transcription under the
        // selected source prefix, whether represented by a collection or a
        // Frame parameter.
        if (containsSelectedPayload(namesVector) ||
            containsSelectedPayload(frame.getParameterKeys<int>()) ||
            containsSelectedPayload(frame.getParameterKeys<float>()) ||
            containsSelectedPayload(frame.getParameterKeys<double>()) ||
            containsSelectedPayload(frame.getParameterKeys<std::string>())) {
          ++stats.offPayloadPresenceFailures;
        }
        continue;
      }

      // BadEventCode is the unmodified AAFLAG/IBAD snapshot. PSFBTG does not
      // update it when IERRBS != 0, so AlgorithmInvoked and Valid are mandatory
      // current-event metadata for recalculated output.
      const auto badEventCode =
          frame.getParameter<int>(valuePrefix + "BadEventCode");
      const auto algorithmInvoked =
          frame.getParameter<int>(valuePrefix + "AlgorithmInvoked");
      const auto tagValid = frame.getParameter<int>(valuePrefix + "Valid");
      bool validityInputsKnown = false;
      bool expectedAlgorithmInvoked = false;
      bool expectedTagValid = false;
      if (expectedPayloadRecalculated) {
        if (!beamSpotErrorCode) validityContractFailure = true;
        if (!badEventCode) {
          ++stats.missingEventParameters;
          validityContractFailure = true;
        }
        if (!algorithmInvoked) {
          ++stats.missingEventParameters;
          validityContractFailure = true;
        }
        if (!tagValid) {
          ++stats.missingEventParameters;
          validityContractFailure = true;
        }

        if (badEventCode && (*badEventCode < 0 || *badEventCode > 2)) {
          validityContractFailure = true;
        }
        if (algorithmInvoked && *algorithmInvoked != 0 &&
            *algorithmInvoked != 1) {
          validityContractFailure = true;
        }
        if (tagValid && *tagValid != 0 && *tagValid != 1) {
          validityContractFailure = true;
        }

        if (beamSpotErrorCode) {
          expectedAlgorithmInvoked = *beamSpotErrorCode == 0;
          if (!expectedAlgorithmInvoked) ++stats.aabtagNotInvokedEvents;
        }
        if (beamSpotErrorCode && badEventCode) {
          validityInputsKnown = true;
          expectedTagValid =
              expectedAlgorithmInvoked && *badEventCode == 0;
          if (!expectedTagValid) ++stats.aabtagInvalidEvents;
          if (expectedAlgorithmInvoked && *badEventCode != 0) {
            ++stats.badStatusEvents;
          }
        }
        if (algorithmInvoked && beamSpotErrorCode &&
            (*algorithmInvoked != (expectedAlgorithmInvoked ? 1 : 0))) {
          validityContractFailure = true;
        }
        if (tagValid && validityInputsKnown &&
            (*tagValid != (expectedTagValid ? 1 : 0))) {
          validityContractFailure = true;
        }
      }

      const bool requireValidPayload =
          expectedPayloadRecalculated && validityInputsKnown && expectedTagValid;
      const bool requireInvalidPayload =
          expectedPayloadRecalculated && validityInputsKnown && !expectedTagValid;

      std::uint64_t finiteEventPayloadValues = 0;
      for (const auto* suffix : {"ProbNegIP", "ProbPosIP", "ProbAllIP"}) {
        const auto value =
            frame.getParameter<std::vector<float>>(valuePrefix + suffix);
        if (!value) {
          ++stats.missingEventParameters;
          continue;
        }
        if (value->size() != 3) {
          ++stats.eventVectorLengthFailures;
          continue;
        }
        for (const float x : *value) {
          if (std::isfinite(x)) ++finiteEventPayloadValues;
          std::uint64_t ignoredNonfinite = 0;
          checkProbability(x, !requireValidPayload, stats.eventProbabilityNans,
                           ignoredNonfinite,
                           stats.eventProbabilityRangeFailures);
          stats.eventProbabilityRangeFailures += ignoredNonfinite;
          if (requireInvalidPayload && !std::isnan(x)) {
            ++stats.eventProbabilityRangeFailures;
            validityContractFailure = true;
          } else if (requireValidPayload && !std::isfinite(x)) {
            validityContractFailure = true;
          }
        }
      }

      const auto thrustAxis =
          frame.getParameter<std::vector<float>>(valuePrefix + "ThrustAxis");
      if (!thrustAxis) {
        ++stats.missingEventParameters;
      } else if (thrustAxis->size() != 3) {
        ++stats.eventVectorLengthFailures;
      } else {
        for (const float x : *thrustAxis) {
          if (std::isfinite(x)) ++finiteEventPayloadValues;
          if (std::isnan(x)) {
            ++stats.thrustNans;
          } else if (!std::isfinite(x) || x < -1.000001f || x > 1.000001f) {
            ++stats.thrustRangeFailures;
          }
          if (requireInvalidPayload && !std::isnan(x)) {
            ++stats.thrustRangeFailures;
            validityContractFailure = true;
          } else if (requireValidPayload && !std::isfinite(x)) {
            validityContractFailure = true;
          }
        }
      }

      const auto thrustValue =
          frame.getParameter<float>(valuePrefix + "ThrustValue");
      if (!thrustValue) {
        ++stats.missingEventParameters;
      } else {
        if (std::isfinite(*thrustValue)) ++finiteEventPayloadValues;
        std::uint64_t ignoredNonfinite = 0;
        checkProbability(*thrustValue, !requireValidPayload, stats.thrustNans,
                         ignoredNonfinite, stats.thrustRangeFailures);
        stats.thrustRangeFailures += ignoredNonfinite;
        if (requireInvalidPayload && !std::isnan(*thrustValue)) {
          ++stats.thrustRangeFailures;
          validityContractFailure = true;
        } else if (requireValidPayload && !std::isfinite(*thrustValue)) {
          validityContractFailure = true;
        }
      }

      if (!expectedPayloadRecalculated) {
        // Historical stored-bank output may legitimately contain some NaN
        // sentinels, but a frame with no finite event-level BTAG word at all is
        // indistinguishable from a missing/unread bank and must not PASS.
        if (finiteEventPayloadValues == 0) ++stats.bankPayloadEmptyFailures;
        continue;
      }

      const auto nTracks = frame.getParameter<int>(valuePrefix + "NTracks");
      const auto nAttached =
          frame.getParameter<int>(valuePrefix + "NTracksAttached");
      const auto nTracksRaw =
          frame.getParameter<int>(valuePrefix + "NTracksRaw");
      const auto truncated =
          frame.getParameter<int>(valuePrefix + "Truncated");
      if (!nTracks) ++stats.missingTrackParameters;
      if (!nAttached) ++stats.missingTrackParameters;
      if (!nTracksRaw) ++stats.missingTrackParameters;
      if (!truncated) ++stats.missingTrackParameters;
      if (nTracksRaw && (*nTracksRaw < 0 || *nTracksRaw > 100)) {
        ++stats.truncationFailures;
        validityContractFailure = true;
      }

      const int expectedTracks = nTracks.value_or(-1);
      if (nTracks) {
        if (*nTracks < 0 || *nTracks > 100) {
          ++stats.truncationFailures;
        } else {
          stats.tracks += static_cast<std::uint64_t>(*nTracks);
        }
      }
      if (truncated) {
        if (*truncated == 1) {
          ++stats.truncatedEvents;
          const int capacityCount = nTracksRaw.value_or(expectedTracks);
          if (capacityCount != 100) ++stats.truncationFailures;
        } else if (*truncated != 0) {
          ++stats.truncationFailures;
        }
      }
      if (nAttached &&
          (*nAttached < 0 || (nTracks && *nAttached > *nTracks))) {
        ++stats.attachedConsistencyFailures;
      }
      if (validityInputsKnown) {
        if (!expectedTagValid &&
            ((nTracks && *nTracks != 0) ||
             (nAttached && *nAttached != 0))) {
          ++stats.attachedConsistencyFailures;
          validityContractFailure = true;
        }
        if (!expectedAlgorithmInvoked && nTracksRaw && *nTracksRaw != 0) {
          ++stats.truncationFailures;
          validityContractFailure = true;
        }
        if (expectedTagValid && nTracks && nTracksRaw &&
            *nTracks != *nTracksRaw) {
          ++stats.truncationFailures;
          validityContractFailure = true;
        }
        if (truncated && nTracksRaw) {
          const int expectedTruncated =
              expectedTagValid && *nTracksRaw == 100 ? 1 : 0;
          if (*truncated != expectedTruncated) {
            ++stats.truncationFailures;
            validityContractFailure = true;
          }
        }
      }

      const std::string pvName = valuePrefix + "PrimaryVertex";
      if (hasCollection(names, pvName, stats)) {
        const auto& pv = frame.get<edm4hep::VertexCollection>(pvName);
        if (validityInputsKnown) {
          const std::size_t expectedPvSize = expectedTagValid ? 1u : 0u;
          if (pv.size() != expectedPvSize) {
            ++stats.pvSizeFailures;
            validityContractFailure = true;
          }
        }
        if (requireValidPayload && pv.size() == 1) {
          const auto vertex = pv[0];
          bool pvContentFailure = false;
          if (!vertex.isPrimary()) {
            ++stats.pvPrimaryFlagFailures;
            pvContentFailure = true;
          }
          if (vertex.getAlgorithmType() !=
              domain::kPrimaryVertexAlgorithmType) {
            ++stats.pvAlgorithmTypeFailures;
            pvContentFailure = true;
          }

          const auto position = vertex.getPosition();
          bool allFinite = domain::isFinite(position.x) &&
                           domain::isFinite(position.y) &&
                           domain::isFinite(position.z) &&
                           domain::isFinite(vertex.getChi2());
          for (const auto value : vertex.getCovMatrix()) {
            allFinite = allFinite && domain::isFinite(value);
          }
          if (!allFinite) {
            // Count once per malformed PV, independent of how many scalar
            // components were corrupted.
            ++stats.pvNonfiniteFailures;
            pvContentFailure = true;
          }
          if (!domain::isSensiblePrimaryVertexNdf(vertex.getNdf())) {
            ++stats.pvNdfDomainFailures;
            pvContentFailure = true;
          }
          if (pvContentFailure) validityContractFailure = true;
        }
      }

      std::size_t particleCount = 0;
      bool haveParticles = false;
      const std::string particlesName = options.source + "_MAIN_Particles";
      if (hasCollection(names, particlesName, stats)) {
        particleCount =
            frame.get<edm4hep::ReconstructedParticleCollection>(
                     particlesName)
                .size();
        haveParticles = true;
      }

      const auto* particleIndex = getUserData<std::int32_t>(
          frame, names, valuePrefix + "Tracks_ParticleIndex", stats);
      const auto* usedForTag = getUserData<std::int32_t>(
          frame, names, valuePrefix + "Tracks_UsedForTag", stats);
      const auto* attached = getUserData<std::int32_t>(
          frame, names, valuePrefix + "Tracks_AttachedToPV", stats);
      const auto* hitsRPhi = getUserData<std::int32_t>(
          frame, names, valuePrefix + "Tracks_NVDHitsRPhi", stats);
      const auto* hitsZ = getUserData<std::int32_t>(
          frame, names, valuePrefix + "Tracks_NVDHitsZ", stats);
      const auto* layersRPhi = getUserData<std::int32_t>(
          frame, names, valuePrefix + "Tracks_NVDLayersRPhi", stats);
      const auto* layersZ = getUserData<std::int32_t>(
          frame, names, valuePrefix + "Tracks_NVDLayersZ", stats);

      const auto* impactRPhi = getUserData<float>(
          frame, names, valuePrefix + "Tracks_ImpactParRPhi", stats);
      const auto* impactRPhiError = getUserData<float>(
          frame, names, valuePrefix + "Tracks_ImpactParRPhiError", stats);
      const auto* impactZ = getUserData<float>(
          frame, names, valuePrefix + "Tracks_ImpactParZ", stats);
      const auto* impactZError = getUserData<float>(
          frame, names, valuePrefix + "Tracks_ImpactParZError", stats);
      const auto* probRPhi = getUserData<float>(
          frame, names, valuePrefix + "Tracks_ProbRPhi", stats);
      const auto* probZ = getUserData<float>(
          frame, names, valuePrefix + "Tracks_ProbZ", stats);
      const auto* chi2Vd = getUserData<float>(
          frame, names, valuePrefix + "Tracks_Chi2VD", stats);
      const auto* chi2Pv = getUserData<float>(
          frame, names, valuePrefix + "Tracks_Chi2PV", stats);
      const auto* momentum = getUserData<float>(
          frame, names, valuePrefix + "Tracks_Momentum", stats);

      const auto checkSize = [&](const auto* collection) {
        if (collection && expectedTracks >= 0 &&
            collection->size() != static_cast<std::size_t>(expectedTracks)) {
          ++stats.trackVectorLengthFailures;
          if (requireInvalidPayload) validityContractFailure = true;
        }
      };
      checkSize(particleIndex);
      checkSize(usedForTag);
      checkSize(attached);
      checkSize(hitsRPhi);
      checkSize(hitsZ);
      checkSize(layersRPhi);
      checkSize(layersZ);
      checkSize(impactRPhi);
      checkSize(impactRPhiError);
      checkSize(impactZ);
      checkSize(impactZError);
      checkSize(probRPhi);
      checkSize(probZ);
      checkSize(chi2Vd);
      checkSize(chi2Pv);
      checkSize(momentum);

      if (particleIndex) {
        for (const auto index : *particleIndex) {
          if (index == -1) {
            ++stats.unresolvedParticleIndices;
          } else if (index < -1 ||
                     (haveParticles && static_cast<std::size_t>(index) >= particleCount)) {
            ++stats.particleIndexRangeFailures;
          }
        }
      }

      const auto markValidPayloadFailure = [&] {
        if (requireValidPayload) validityContractFailure = true;
      };
      for (const auto* impacts : {impactRPhi, impactZ}) {
        if (!impacts) continue;
        for (const float value : *impacts) {
          if (!domain::isFinite(value)) {
            ++stats.trackImpactNonfiniteFailures;
            markValidPayloadFailure();
          }
        }
      }
      for (const auto* errors : {impactRPhiError, impactZError}) {
        if (!errors) continue;
        for (const float value : *errors) {
          if (!domain::isPositiveFinite(value)) {
            ++stats.trackErrorDomainFailures;
            markValidPayloadFailure();
          }
        }
      }
      for (const auto* chi2Values : {chi2Vd, chi2Pv}) {
        if (!chi2Values) continue;
        for (const float value : *chi2Values) {
          if (!domain::isNonnegativeFinite(value)) {
            ++stats.trackChi2DomainFailures;
            markValidPayloadFailure();
          }
        }
      }
      if (momentum) {
        for (const float value : *momentum) {
          if (!domain::isPositiveFinite(value)) {
            ++stats.trackMomentumDomainFailures;
            markValidPayloadFailure();
          }
        }
      }

      if (usedForTag) {
        for (const auto value : *usedForTag) {
          if (!domain::isValidUsedForTag(value)) {
            ++stats.trackIntegerDomainFailures;
            markValidPayloadFailure();
          }
        }
      }
      if (attached) {
        for (const auto value : *attached) {
          if (!domain::isValidAttachedFlag(value)) {
            ++stats.trackIntegerDomainFailures;
            markValidPayloadFailure();
          }
        }
      }
      const auto checkSignedCounts = [&](const auto* counts, int maximum) {
        if (!counts) return;
        for (const auto value : *counts) {
          if (!domain::isValidSignedCount(value, maximum)) {
            ++stats.trackIntegerDomainFailures;
            markValidPayloadFailure();
          }
        }
      };
      checkSignedCounts(hitsRPhi, domain::kMaxVdHits);
      checkSignedCounts(hitsZ, domain::kMaxVdHits);
      checkSignedCounts(layersRPhi, domain::kMaxVdLayers);
      checkSignedCounts(layersZ, domain::kMaxVdLayers);

      if (attached && nAttached && *nAttached >= 0) {
        std::uint64_t attachedCount = 0;
        bool badValue = false;
        for (const auto value : *attached) {
          if (value == 1) {
            ++attachedCount;
          } else if (value != 0) {
            badValue = true;
          }
        }
        if (badValue || attachedCount != static_cast<std::uint64_t>(*nAttached)) {
          ++stats.attachedConsistencyFailures;
        }
      }

      for (const auto* probabilities : {probRPhi, probZ}) {
        if (!probabilities) continue;
        for (const float value : *probabilities) {
          std::uint64_t ignoredNanCounter = 0;
          checkProbability(value, false, ignoredNanCounter,
                           stats.trackProbabilityNonfiniteFailures,
                           stats.trackProbabilityRangeFailures);
        }
      }
      if (validityContractFailure) ++stats.validityContractFailures;
    }
  } catch (const std::exception& error) {
    std::cerr << "error: validation failed: " << error.what() << "\n";
    ++stats.frameReadFailures;
  }

  printResult(stats, options, expectedRecalculated, referenceEntries);
  return stats.failures() == 0 ? 0 : 2;
}
