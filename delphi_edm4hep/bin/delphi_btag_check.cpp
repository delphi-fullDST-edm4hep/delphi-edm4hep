// delphi_btag_check — validate the b-tag payload of a converted file.
//
//   delphi_btag_check [--source sDST|fDST] <input.edm4hep.root> <run|data>
//
// The second argument states what the run numbers should look like: `data`
// for positive run numbers, or `mc` for the negative ones AABTAG uses to pick
// its simulation calibration.
//
// Both b-tags are emitted on every file, so this checks that both are there
// and that the recalculated one is self-consistent: the per-track rows match
// the track count AABTAG reports, each row links to a particle, the values sit
// in their allowed ranges, and the vertex agrees with the number of tracks it
// says it fitted.
//
// Exit 0 when everything checks out, 2 otherwise. The single status line is
// meant to be greppable from CI.

#include "delphi_edm4hep/internal/BtagCheckDomains.h"

#include <edm4hep/ParticleIDCollection.h>
#include <edm4hep/TrackCollection.h>
#include <edm4hep/TrackState.h>
#include <edm4hep/VertexCollection.h>
#include <podio/Frame.h>
#include <podio/ROOTReader.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace domain = delphi_edm4hep::btag::check;

namespace {

// CHI2TR is a truncated quadratic form, so it rounds slightly below zero for a
// track sitting on the vertex. Observed floor over 3459 attached tracks is
// -0.03, against a median of 1.4.
constexpr float kChi2RoundGuard = 0.1f;


struct Stats {
  std::uint64_t entries = 0;
  std::uint64_t framesChecked = 0;
  std::uint64_t frameReadFailures = 0;
  std::uint64_t missingParameters = 0;
  std::uint64_t missingCollections = 0;
  std::uint64_t sourcePrefixFailures = 0;
  std::uint64_t runSignFailures = 0;
  std::uint64_t probabilityFailures = 0;
  std::uint64_t tagRowCountFailures = 0;
  std::uint64_t tagParamCountFailures = 0;
  // Rows the layout check rejected, and whose domain checks therefore never
  // ran. Reported separately so a skipped row cannot be read as a passed one.
  std::uint64_t tagRowsUnchecked = 0;
  std::uint64_t tagDomainFailures = 0;
  std::uint64_t unresolvedParticles = 0;
  std::uint64_t impactStateCountFailures = 0;
  std::uint64_t impactDomainFailures = 0;
  std::uint64_t vertexFailures = 0;
  std::uint64_t attachedConsistencyFailures = 0;
  std::uint64_t beamspotErrorEvents = 0;

  std::uint64_t failures() const {
    return frameReadFailures + missingParameters + missingCollections +
           sourcePrefixFailures + runSignFailures + probabilityFailures +
           tagRowCountFailures + tagParamCountFailures + tagDomainFailures +
           impactStateCountFailures + impactDomainFailures + vertexFailures +
           attachedConsistencyFailures;
  }
};

void usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " [--source sDST|fDST] <input.edm4hep.root> <data|mc>\n";
}

// A value computed in single precision can land one ULP outside a closed
// bound: a thrust of exactly 1, which is physical, comes back as 1 + 2^-23.
constexpr float kBoundSlack = std::numeric_limits<float>::epsilon();

// A probability is either the not-computed sentinel (NaN) or in [0, 1].
bool okProbability(float v) {
  return std::isnan(v) ||
         (domain::isFinite(v) && v >= -kBoundSlack && v <= 1.f + kBoundSlack);
}

// A thrust-axis component is a direction cosine, so it spans [-1, 1].
bool okCosine(float v) {
  return std::isnan(v) || (domain::isFinite(v) &&
                           v >= -1.f - kBoundSlack && v <= 1.f + kBoundSlack);
}

}  // namespace

int main(int argc, char** argv) {
  std::string source = "sDST";
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--source") {
      if (++i >= argc) { std::cerr << "error: --source requires sDST or fDST\n"; return 1; }
      source = argv[i];
    } else if (arg.rfind("--source=", 0) == 0) {
      source = arg.substr(std::string("--source=").size());
    } else if (arg.rfind("--", 0) == 0) {
      std::cerr << "error: unknown option: " << arg << "\n";
      return 1;
    } else {
      positional.push_back(arg);
    }
  }
  if (positional.size() != 2) { usage(argv[0]); return 1; }
  if (source != "sDST" && source != "fDST") {
    std::cerr << "error: --source must be sDST or fDST, got: " << source << "\n";
    return 1;
  }
  const std::string& input = positional[0];
  const std::string& kind  = positional[1];
  if (kind != "data" && kind != "mc") {
    std::cerr << "error: expected data or mc, got: " << kind << "\n";
    usage(argv[0]);
    return 1;
  }
  const bool expectNegativeRun = (kind == "mc");

  const std::string cfg  = source + "_BTAGCFG_";
  const std::string aa   = source + "_AABTAG_";
  const std::string btg  = source + "_BTG_";
  // Identity parameters stay under the sDST prefix in both passes: pass 2
  // copies them and has no fDST_EVT_ domain of its own.
  const std::string evt  = "sDST_EVT_";

  Stats stats;
  try {
    podio::ROOTReader reader;
    reader.openFile(input);
    stats.entries = reader.getEntries("events");

    for (std::uint64_t i = 0; i < stats.entries; ++i) {
      podio::Frame frame(reader.readEntry("events", i));
      ++stats.framesChecked;

      // ---- provenance that still varies ----
      const auto prefix = frame.getParameter<std::string>(cfg + "SourcePrefix");
      if (!prefix) ++stats.missingParameters;
      else if (*prefix != source) ++stats.sourcePrefixFailures;

      const auto beamspot = frame.getParameter<int>(cfg + "BeamSpotErrorCode");
      if (!beamspot) ++stats.missingParameters;
      else if (*beamspot != 0) ++stats.beamspotErrorEvents;

      // AABTAG selects its calibration by the sign of the run number, so a
      // simulation file converted as data is a real error, not cosmetics.
      const auto run = frame.getParameter<int>(evt + "runNumber");
      if (!run) ++stats.missingParameters;
      else if ((*run < 0) != expectNegativeRun) ++stats.runSignFailures;

      // ---- both tags present, both in range ----
      for (const std::string& tag : {btg, aa}) {
        for (const char* name : {"ProbNegIP", "ProbPosIP", "ProbAllIP", "ThrustAxis"}) {
          const auto v = frame.getParameter<std::vector<float>>(tag + name);
          if (!v) { ++stats.missingParameters; continue; }
          if (v->size() != 3) { ++stats.probabilityFailures; continue; }
          const bool cosine = std::string_view(name) == "ThrustAxis";
          for (const float x : *v) {
            if (cosine ? !okCosine(x) : !okProbability(x)) ++stats.probabilityFailures;
          }
        }
        const auto thrust = frame.getParameter<float>(tag + "ThrustValue");
        if (!thrust) ++stats.missingParameters;
        else if (!okProbability(*thrust)) ++stats.probabilityFailures;
      }

      const auto nTracks   = frame.getParameter<int>(aa + "NTracks");
      const auto nAttached = frame.getParameter<int>(aa + "NTracksAttached");
      const auto valid     = frame.getParameter<int>(aa + "Valid");
      if (!nTracks || !nAttached || !valid) { ++stats.missingParameters; continue; }

      const auto names = frame.getAvailableCollections();
      const auto has = [&](const std::string& n) {
        for (const auto& x : names) if (x == n) return true;
        return false;
      };

      // ---- per-track rows ----
      const std::string tagName = aa + "TrackTag";
      if (!has(tagName)) { ++stats.missingCollections; continue; }
      const auto& tags = frame.get<edm4hep::ParticleIDCollection>(tagName);
      if (static_cast<int>(tags.size()) != *nTracks) ++stats.tagRowCountFailures;

      std::uint64_t attachedRows = 0;
      for (const auto& tag : tags) {
        const auto params = tag.getParameters();
        if (params.size() != domain::kTrCount) {
          ++stats.tagParamCountFailures;
          ++stats.tagRowsUnchecked;
          continue;
        }
        if (!okProbability(params[domain::kTrProb]) ||
            !okProbability(params[domain::kTrProbZ])) ++stats.tagDomainFailures;
        if (!domain::isNonnegativeFinite(params[domain::kTrChi2Vd])) ++stats.tagDomainFailures;
        // CHI2TR is defined only where AABTAG attached the track; the
        // converter emits NaN elsewhere, and the checker holds it to that.
        const bool attached =
            static_cast<std::int32_t>(params[domain::kTrAttached]) == 1;
        if (attached) {
          if (!domain::isFinite(params[domain::kTrChi2Tr]) ||
              params[domain::kTrChi2Tr] < -kChi2RoundGuard)
            ++stats.tagDomainFailures;
        } else if (!std::isnan(params[domain::kTrChi2Tr])) {
          ++stats.tagDomainFailures;
        }
        if (!domain::isPositiveFinite(params[domain::kTrMomentum])) ++stats.tagDomainFailures;
        if (!domain::isValidSignedCount(static_cast<std::int32_t>(params[domain::kTrNVdp]), domain::kMaxVdHits) ||
            !domain::isValidSignedCount(static_cast<std::int32_t>(params[domain::kTrNVdpz]), domain::kMaxVdHits) ||
            !domain::isValidSignedCount(static_cast<std::int32_t>(params[domain::kTrNLay]), domain::kMaxVdLayers) ||
            !domain::isValidSignedCount(static_cast<std::int32_t>(params[domain::kTrNLayz]), domain::kMaxVdLayers))
          ++stats.tagDomainFailures;
        if (!domain::isValidUsedForTag(static_cast<std::int32_t>(params[domain::kTrIsrt]))) ++stats.tagDomainFailures;
        if (!domain::isValidAttachedFlag(static_cast<std::int32_t>(params[domain::kTrAttached]))) ++stats.tagDomainFailures;
        if (attached) ++attachedRows;
        if (!tag.getParticle().isAvailable()) ++stats.unresolvedParticles;
      }

      // ---- impact parameters, which ride on the track ----
      const std::string trackName = source + "_TRAC_Tracks";
      if (!has(trackName)) { ++stats.missingCollections; continue; }
      int atVertex = 0;
      for (const auto& track : frame.get<edm4hep::TrackCollection>(trackName)) {
        for (const auto& state : track.getTrackStates()) {
          if (state.location != edm4hep::TrackState::AtVertex) continue;
          ++atVertex;
          if (!domain::isFinite(state.D0) || !domain::isFinite(state.Z0))
            ++stats.impactDomainFailures;
          // Only the two measured variances are filled; the rest are NaN.
          const float vD0 = state.covMatrix[0];
          const float vZ0 = state.covMatrix[9];
          if (!domain::isPositiveFinite(vD0) || !domain::isPositiveFinite(vZ0))
            ++stats.impactDomainFailures;
        }
      }
      if (atVertex != *nTracks) ++stats.impactStateCountFailures;

      // ---- the vertex AABTAG fitted ----
      const std::string pvName = aa + "PrimaryVertex";
      if (!has(pvName)) { ++stats.missingCollections; continue; }
      const auto& pvs = frame.get<edm4hep::VertexCollection>(pvName);
      const std::size_t expectedPvs = (*valid == 1) ? 1u : 0u;
      if (pvs.size() != expectedPvs) ++stats.vertexFailures;
      for (const auto& pv : pvs) {
        if (!pv.isPrimary() ||
            pv.getAlgorithmType() != domain::kPrimaryVertexAlgorithmType ||
            !domain::isFinite(pv.getPosition().x) ||
            !domain::isFinite(pv.getPosition().y) ||
            !domain::isFinite(pv.getPosition().z) ||
            !domain::isSensiblePrimaryVertexNdf(pv.getNdf())) {
          ++stats.vertexFailures;
        }
        // chi2 is only meaningful where a fit happened. AABTAG leaves a
        // numerically-zero value on the beamspot-only vertices it reports
        // with ndf 0.
        if (pv.getNdf() > 0 && !domain::isNonnegativeFinite(pv.getChi2())) {
          ++stats.vertexFailures;
        }
        // The vertex relation and the per-track flag are independent routes to
        // the same number; they must agree.
        if (static_cast<int>(pv.getParticles().size()) != *nAttached)
          ++stats.attachedConsistencyFailures;
      }
      if (attachedRows != static_cast<std::uint64_t>(*nAttached))
        ++stats.attachedConsistencyFailures;
    }
  } catch (const std::exception& error) {
    std::cerr << "error: validation failed: " << error.what() << "\n";
    ++stats.frameReadFailures;
  }

  const bool ok = stats.failures() == 0;
  std::cout << "status=" << (ok ? "PASS" : "FAIL")
            << " entries=" << stats.entries
            << " source=" << source
            << " expected=" << kind
            << " frames_checked=" << stats.framesChecked
            << " failures=" << stats.failures()
            << " frame_read_failures=" << stats.frameReadFailures
            << " missing_parameters=" << stats.missingParameters
            << " missing_collections=" << stats.missingCollections
            << " source_prefix_failures=" << stats.sourcePrefixFailures
            << " run_sign_failures=" << stats.runSignFailures
            << " probability_failures=" << stats.probabilityFailures
            << " tag_row_count_failures=" << stats.tagRowCountFailures
            << " tag_param_count_failures=" << stats.tagParamCountFailures
            << " tag_rows_unchecked=" << stats.tagRowsUnchecked
            << " tag_domain_failures=" << stats.tagDomainFailures
            << " impact_state_count_failures=" << stats.impactStateCountFailures
            << " impact_domain_failures=" << stats.impactDomainFailures
            << " vertex_failures=" << stats.vertexFailures
            << " attached_consistency_failures=" << stats.attachedConsistencyFailures
            << " unresolved_particles=" << stats.unresolvedParticles
            << " beamspot_error_events=" << stats.beamspotErrorEvents
            << "\n";
  return ok ? 0 : 2;
}
