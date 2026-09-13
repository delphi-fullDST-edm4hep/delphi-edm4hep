// Tracking domain — implementation S3 (pass-1).
//
// Walks the PA chain (LDTOP-1 -> per-PV -> per-PA), emitting:
//   <prefix>_TRAC_Tracks    (Track + TrackState[AtIP] + 5x5 helix-basis cov)
//   <prefix>_MAIN_Particles (charged + neutral; converter-owned PA view)
//   <prefix>_VECP_Particles_SelectionFlag       (UserData int32)
//   <prefix>_MAIN_Particles_ReconstructionCode  (UserData int32)
//   <prefix>_MAIN_Particles_DetectorMask        (UserData int32)
//   <prefix>_MAIN_Particles_ChargeCode           (UserData int32)
//
// Each Track links to the track elements from its PA (see TrackElements).
//   <prefix>_MAIN_Particles_TrackLength         (UserData float, cm)
//   <prefix>_QTRAC_Tracks_d0PV / _z0PV / _d0BS  (UserData float; QTRAC 38..40)
//
// No shape moments, no sigma calibration, no perigee-momentum fallback —
// bank-truth values only (those custom operations are intentionally
// dropped).

#include "delphi_edm4hep/Tracking/Tracking.h"

#include "delphi_edm4hep/Event/EventInfo.h"
#include "delphi_edm4hep/internal/AabtagTrackState.h"

#include "delphi_edm4hep/Helix.h"
#include "delphi_edm4hep/internal/PaWalk.h"

#include <edm4hep/ReconstructedParticleCollection.h>
#include <edm4hep/TrackCollection.h>
#include <edm4hep/TrackState.h>
#include <podio/Frame.h>
#include <podio/UserDataCollection.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace ph = phdst;
namespace aa = delphi_edm4hep::aabtag;

namespace {
constexpr double kCm2Mm = 10.0;
constexpr float kPionMass = 0.1395675f;
}  // namespace

namespace delphi_edm4hep::tracking {

namespace {

// The DELPHI perigee -> EDM4hep helix conversion (Jacobian push-forward,
// weight-matrix inversion, TrackState emit) lives in the shared
// delphi_edm4hep::Helix class (Helix.h) so PA.TRAC and PA.ELTR use one
// definition.

extern "C" void tbdcae_(float* trajectory, float* vertex,
                         float* covariance, float* dca);
extern "C" void confpv_(float* vertex, float* vertexCovariance,
                         float* probabilityCut, float* trajectory,
                         float* main, float* covarianceIn,
                         float* covarianceOut, float* chi2, int* error);
extern "C" void fkmi5_(float* weight, float* covariance, int* error);
extern "C" float vmod_(float* values, int* count);

float vectorMagnitude2(float first, float second) {
  std::array<float, 2> values{{first, second}};
  int count = 2;
  return vmod_(values.data(), &count);
}

struct ParticleView {
  int lpa = 0;
  bool charged = false;
  std::array<float, 3> momentum{};
  float energy = 0.f;
  float mass = 0.f;
  std::int32_t selection = 0;
  std::array<float, 3> impact{{-999.f, -999.f, -999.f}};
};

std::array<float, 5> propagatePerigee(float beamX, float beamY,
                                       const std::array<float, 5>& input) {
  if (beamX == 0.f && beamY == 0.f) return input;
  // PSPGBM first constructs RPARI in REAL and only then promotes it to
  // DOUBLE PRECISION inside PSRDCA. Preserve that intermediate rounding;
  // constructing x/y directly in double changes occasional output ULPs.
  const float xSingle = input[0] * std::sin(input[3]);
  const float ySingle = -input[0] * std::cos(input[3]);
  const double z0 = input[1];
  const double theta = input[2];
  const double phi = input[3];
  const double curvature = input[4];
  const double x = xSingle;
  const double y = ySingle;
  const double sp = (x - beamX) * std::cos(phi) +
                    (y - beamY) * std::sin(phi);
  const double cp = (x - beamX) * std::sin(phi) -
                    (y - beamY) * std::cos(phi);
  double xFinal = 0.0;
  double yFinal = 0.0;
  double zFinal = 0.0;
  double deltaPhi = 0.0;
  if (curvature != 0.0) {
    deltaPhi = std::atan2(-curvature * sp, 1.0 - curvature * cp);
    const double distance = deltaPhi / (curvature * std::sin(theta));
    xFinal = x + std::cos(phi) * std::sin(deltaPhi) / curvature -
             2.0 * std::sin(phi) * std::pow(std::sin(0.5 * deltaPhi), 2) /
                 curvature;
    yFinal = y + std::sin(phi) * std::sin(deltaPhi) / curvature +
             2.0 * std::cos(phi) * std::pow(std::sin(0.5 * deltaPhi), 2) /
                 curvature;
    zFinal = z0 + distance * std::cos(theta);
  } else {
    xFinal = x - sp * std::cos(phi);
    yFinal = y - sp * std::sin(phi);
    zFinal = z0 - sp * std::cos(theta) / std::sin(theta);
  }
  const double phiFinal = phi + deltaPhi;
  const double d0Final = std::abs(std::sin(phiFinal)) >= 0.7
      ? (xFinal - beamX) / std::sin(phiFinal)
      : -(yFinal - beamY) / std::cos(phiFinal);
  return {static_cast<float>(d0Final), static_cast<float>(zFinal),
          input[2], static_cast<float>(phiFinal), input[4]};
}

struct RefitResult {
  bool accepted = false;
  std::array<float, 20> trajectory{};
  float momentum = 0.f;
  float energy = 0.f;
  float charge = 0.f;
  float momentumError = 0.f;
};

RefitResult refitWithPrimaryVertex(
    const std::array<float, 3>& primary,
    const std::array<float, 6>& primaryCovariance,
    const std::array<float, 20>& inputTrajectory, int lmain,
    float beamEnergy) {
  RefitResult result;
  result.trajectory = inputTrajectory;
  std::array<float, 31> main{};
  for (int i = 0; i < 31; ++i) main[i] = ph::Q(lmain + 2 + i);
  std::array<float, 36> covarianceIn{};
  std::array<float, 36> covarianceOut{};
  auto vertex = primary;
  auto vertexCovariance = primaryCovariance;
  float probabilityCut = 1.e-2f;
  float chi2 = 0.f;
  int error = 0;
  confpv_(vertex.data(), vertexCovariance.data(), &probabilityCut,
          result.trajectory.data(), main.data(), covarianceIn.data(),
          covarianceOut.data(), &chi2, &error);
  if (error != 0 || main[5] < 0.1f || main[5] >= 1.5f * beamEnergy) {
    return result;
  }

  std::array<float, 15> weight{};
  std::copy_n(result.trajectory.begin() + 5, 15, weight.begin());
  std::array<float, 15> covariance{};
  fkmi5_(weight.data(), covariance.data(), &error);
  float relativeError = 1.f;
  if (error == 0) {
    const float fieldFactor = event::current().magneticFieldGevPerCm;
    const float curvature = result.trajectory[4];
    const float theta = result.trajectory[2];
    const float momentumErrorSquared =
        std::pow(fieldFactor / curvature / std::sin(theta), 2) *
        (covariance[14] / std::pow(curvature, 2) +
         covariance[5] / std::pow(std::tan(theta), 2) +
         2.f * covariance[12] / std::tan(theta) / curvature);
    result.momentumError = std::sqrt(momentumErrorSquared);
    relativeError = result.momentumError /
                    std::abs(fieldFactor / curvature / std::sin(theta));
  }
  if (relativeError >= 1.f) return result;

  result.accepted = true;
  result.energy = main[4];
  result.momentum = main[5];
  result.charge = main[6];
  return result;
}

std::array<float, 4> impactAt(const std::array<float, 20>& trajectory,
                              const std::array<float, 3>& vertex,
                              const std::array<float, 6>& covariance) {
  auto mutableTrajectory = trajectory;
  auto mutableVertex = vertex;
  auto mutableCovariance = covariance;
  std::array<float, 4> dca{{-999.f, -999.f, -999.f, -999.f}};
  tbdcae_(mutableTrajectory.data(), mutableVertex.data(),
          mutableCovariance.data(), dca.data());
  return dca;
}

std::vector<ParticleView> buildParticleViews() {
  std::vector<ParticleView> charged;
  std::vector<ParticleView> neutral;
  if (ph::LDTOP <= 0) return {ParticleView{}};

  std::array<float, 3> primary{};
  std::array<float, 6> primaryCovariance{};
  const int primaryBank = ph::LQ(ph::LDTOP - 1);
  if (primaryBank > 0) {
    for (int i = 0; i < 3; ++i) primary[i] = ph::Q(primaryBank + 5 + i);
    for (int i = 0; i < 6; ++i) {
      primaryCovariance[i] = ph::Q(primaryBank + 9 + i);
    }
  }
  const auto& beamSpot = event::current().beamSpot.positionCm;
  const std::array<float, 3> beam{{beamSpot[0], beamSpot[1], beamSpot[2]}};
  const std::array<float, 6> zeroCovariance{};
  const float beamEnergy = event::current().centreOfMassEnergy * 0.5f;

  for (int lpv = primaryBank; lpv > 0; lpv = ph::LQ(lpv)) {
    const bool lockedVertex = (ph::IQ(lpv) & (0x02 | 0x04)) != 0;
    for (int lpa = ph::LQ(lpv - 1); lpa > 0; lpa = ph::LQ(lpa)) {
      const int lmain = pawalk::lphpa("MAIN", lpa);
      if (lmain <= 0) continue;
      const int chargeCode = std::lround(ph::Q(lmain + 8));
      ParticleView view;
      view.lpa = lpa;
      if (lockedVertex) view.selection |= static_cast<std::int32_t>(0x80000000U);

      if (chargeCode == 0) {
        const float p = std::abs(ph::Q(lmain + 7));
        if (p > 1.e-5f) {
          view.momentum = {ph::Q(lmain + 3), ph::Q(lmain + 4),
                           ph::Q(lmain + 5)};
          // PSHVEC takes |p| from MAIN+7 and VMOD with zero mass, rather
          // than recomputing it from the three rounded components.
          view.energy = p;
        }
        neutral.push_back(view);
        continue;
      }

      const int ltrac = pawalk::lphpa("TRAC", lpa);
      if (ltrac <= 0) continue;
      view.charged = true;
      view.mass = kPionMass;
      const float p = std::abs(ph::Q(lmain + 7));
      std::array<float, 5> perigee{};
      std::array<float, 20> trajectory{};
      for (int i = 0; i < 5; ++i) perigee[i] = ph::Q(ltrac + 2 + i);
      for (int i = 0; i < 20; ++i) trajectory[i] = ph::Q(ltrac + 2 + i);
      float effectiveP = p;
      float effectiveMomentumError = ph::Q(lmain + 19);
      const int reconstructionCode =
          (static_cast<std::uint32_t>(ph::IQ(lpa + 3)) >> 18) & 0x7fU;
      const std::uint32_t detectors =
          static_cast<std::uint32_t>(ph::IQ(lpa + 2));
      const bool forwardOnly =
          (detectors & ((1U << 3) | (1U << 4) | (1U << 5) | (1U << 8))) == 0 &&
          (detectors & (1U << 20)) != 0 && (detectors & (1U << 25)) != 0;
      if (reconstructionCode == 75 || forwardOnly) {
        const auto refit = refitWithPrimaryVertex(
            primary, primaryCovariance, trajectory, lmain, beamEnergy);
        if (refit.accepted) {
          trajectory = refit.trajectory;
          effectiveP = refit.momentum;
          view.energy = refit.energy;
          effectiveMomentumError = refit.momentumError;
        }
      }
      for (int i = 0; i < 5; ++i) perigee[i] = trajectory[i];
      auto propagated = propagatePerigee(primary[0], primary[1], perigee);
      if (effectiveP > 1.e-5f) {
        view.momentum = {
            effectiveP * std::sin(propagated[2]) * std::cos(propagated[3]),
            effectiveP * std::sin(propagated[2]) * std::sin(propagated[3]),
            effectiveP * std::cos(propagated[2])};
      }
      if (view.energy == 0.f) {
        view.energy = vectorMagnitude2(view.mass, effectiveP);
      }
      const auto pvDca = impactAt(trajectory, primary, primaryCovariance);
      const auto bsDca = impactAt(trajectory, beam, zeroCovariance);
      view.impact = {pvDca[0], pvDca[1], bsDca[0]};

      bool rejected = effectiveP < 0.1f;
      bool failedImpact = std::abs(pvDca[0]) > 4.f;
      rejected = rejected || failedImpact;
      const float sinTheta = std::sin(propagated[2]);
      if (sinTheta > 0.f) {
        failedImpact = failedImpact || std::abs(pvDca[1]) > 4.f / sinTheta;
        rejected = rejected || std::abs(pvDca[1]) > 4.f / sinTheta;
      }
      const bool highMomentum = effectiveP >= 1.5f * beamEnergy;
      rejected = rejected || highMomentum;
      const float denominator = std::min(effectiveP, beamEnergy);
      if (denominator > 0.f) {
        rejected = rejected || effectiveMomentumError / denominator > 1.f;
      }
      if (event::current().dstVersion < 107) {
        rejected = rejected || reconstructionCode == 77 ||
                   reconstructionCode == 72;
      }
      if (rejected && highMomentum) {
        const auto refit = refitWithPrimaryVertex(
            primary, primaryCovariance, trajectory, lmain, beamEnergy);
        if (refit.accepted) {
          trajectory = refit.trajectory;
          effectiveP = refit.momentum;
          view.energy = refit.energy;
          effectiveMomentumError = refit.momentumError;
          for (int i = 0; i < 5; ++i) perigee[i] = trajectory[i];
          propagated = propagatePerigee(primary[0], primary[1], perigee);
          view.momentum = {
              effectiveP * std::sin(propagated[2]) * std::cos(propagated[3]),
              effectiveP * std::sin(propagated[2]) * std::sin(propagated[3]),
              effectiveP * std::cos(propagated[2])};
          const auto recoveredPv =
              impactAt(trajectory, primary, primaryCovariance);
          const auto recoveredBs = impactAt(trajectory, beam, zeroCovariance);
          view.impact = {recoveredPv[0], recoveredPv[1], recoveredBs[0]};
          rejected = false;
        }
      }
      if (rejected && !failedImpact && (reconstructionCode == 99 ||
                       (reconstructionCode >= 82 && reconstructionCode <= 87) ||
                       (reconstructionCode >= 120 && reconstructionCode <= 126))) {
        // PSHCTRECMAM uses the PA's downstream/end vertex, not the origin
        // vertex whose outgoing chain we are currently walking.
        const int endVertex = ph::LQ(lpa - 1);
        const float recoveredP = endVertex > 0 ? ph::Q(endVertex + 15) : 0.f;
        if (recoveredP > 0.f) {
          const float scale = recoveredP / effectiveP;
          for (float& component : view.momentum) component *= scale;
          effectiveP = recoveredP;
          view.energy = vectorMagnitude2(view.mass, effectiveP);
          rejected = false;
        }
      }
      if (rejected) view.selection |= 1;
      charged.push_back(view);
    }
  }

  std::vector<ParticleView> result(1);  // one-based, matching VECP consumers
  result.reserve(charged.size() + neutral.size() + 1);
  result.insert(result.end(), charged.begin(), charged.end());
  result.insert(result.end(), neutral.begin(), neutral.end());
  return result;
}

}  // namespace

void TrackingWriter::emit()
{
  const auto views = buildParticleViews();
  std::unordered_map<int, int> lpaToView;
  lpaToView.reserve(views.size());
  for (std::size_t i = 1; i < views.size(); ++i) {
    lpaToView.emplace(views[i].lpa, static_cast<int>(i));
  }

  // Storage that will be moved into the Frame at the end. Handles taken
  // before the move remain valid (podio guarantees handle stability
  // across the collection move).
  edm4hep::TrackCollection                 trkCol;
  edm4hep::ReconstructedParticleCollection pfoCol;
  podio::UserDataCollection<std::int32_t>  lvlockCol;
  podio::UserDataCollection<std::int32_t>  codeCol;
  podio::UserDataCollection<std::int32_t>  detCol;
  podio::UserDataCollection<std::int32_t>  chargeCodeCol;
  podio::UserDataCollection<float>         lengthCol;
  podio::UserDataCollection<float>         d0PvCol;
  podio::UserDataCollection<float>         z0PvCol;
  podio::UserDataCollection<float>         d0BsCol;

  Output result;
  result.particle_handles.reserve(views.size());
  // Direct-view index map. The ordering contract remains charged first, then
  // neutral, with index zero unused while downstream writers migrate.
  result.vecp_to_particle.assign(views.size(), -1);
  // PA-index map: sized lazily as we walk; -1 entries for PAs we skip.
  // Reserve a reasonable upper bound to avoid mid-walk reallocation.
  result.pa_to_particle.reserve(views.size() + 16);

  // Helper: register a freshly-created particle handle in the output
  // metadata. vecp_i = 0 means "no VECP match" (the handle still goes
  // into particle_handles, but vecp_to_particle is not updated).
  // The current paIdx is taken from the surrounding forEachPA scope.
  auto record_particle = [&](edm4hep::MutableReconstructedParticle pfo,
                              int vecp_i, int paIdx, int lpa) {
    const int new_idx = static_cast<int>(result.particle_handles.size());
    result.particle_handles.push_back(pfo);
    result.particle_lpas.push_back(lpa);
    if (vecp_i >= 1 && vecp_i < static_cast<int>(result.vecp_to_particle.size())) {
      result.vecp_to_particle[vecp_i] = new_idx;
    }
    if (paIdx >= static_cast<int>(result.pa_to_particle.size())) {
      result.pa_to_particle.resize(static_cast<std::size_t>(paIdx + 1), -1);
    }
    result.pa_to_particle[paIdx] = new_idx;
    result.lpa_to_particle[lpa] = new_idx;
  };

  // Push the direct TBDCAE impact-parameter triplet, converted cm -> mm in the
  // historical DELPHI sign convention.
  static constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
  auto push_qtrac_or_nan = [&](int vecp_i) {
    if (vecp_i >= 1 && vecp_i < static_cast<int>(views.size())) {
      d0PvCol.push_back(views[vecp_i].impact[0] * 10.f);
      z0PvCol.push_back(views[vecp_i].impact[1] * 10.f);
      d0BsCol.push_back(views[vecp_i].impact[2] * 10.f);
    } else {
      d0PvCol.push_back(kNaN);
      z0PvCol.push_back(kNaN);
      d0BsCol.push_back(kNaN);
    }
  };

  using namespace delphi_edm4hep::pawalk;

  // Helper: extend pa_to_particle with -1 so the map stays parallel to
  // the PA-walk index even when we return early for a skipped PA.
  auto mark_pa_skipped = [&](int paIdx) {
    if (paIdx >= static_cast<int>(result.pa_to_particle.size())) {
      result.pa_to_particle.resize(static_cast<std::size_t>(paIdx + 1), -1);
    }
  };

  // Per-particle words running parallel to <tag>_MAIN_Particles.
  //
  //   LVLOCK  converter-owned selection/status word; bit 1 is the charged
  //           track verdict and bit 32 marks a locked vertex assignment.
  //   code    PXPHOT reconstruction code, IQ(LPA+3) bits 19-25. SKELANA reads
  //           it to reject VD-only tracks (75 with z, 77 without z, VFT-only
  //           from ISVER 107) and ID+VD-only tracks without z (72).
  //   length  track length in cm, the one selection quantity that cannot be
  //           reconstructed from the other emitted collections.
  auto push_particle_words = [&](int vecp_i, int lpa, int lmain) {
    lvlockCol.push_back(vecp_i >= 1 ? views[vecp_i].selection : -1);
    codeCol.push_back((ph::IQ(lpa + 3) >> 18) & 0x7F);
    detCol.push_back(ph::IQ(lpa + 2));
    chargeCodeCol.push_back(
        static_cast<std::int32_t>(std::lround(ph::Q(lmain + 8))));
    lengthCol.push_back(lmain > 0 ? ph::Q(lmain + 9) : 0.f);
  };

  // AABTAG's impact parameters, keyed by the PA they belong to. Empty when
  // AABTAG produced nothing usable for this event.
  const auto lpa_to_btag = aabtag::lpaToTrack();

  forEachPA([&](int lpa, int paIdx) {
    // PA.MAIN: per-track summary. Charge code at Q(LMAIN+8):
    //   0 = neutral, 1 = positive, 2 = negative, 3 = undefined.
    const int lmain = lphpa("MAIN", lpa);
    if (lmain <= 0) { mark_pa_skipped(paIdx); return; }
    const int charge_code = static_cast<int>(std::lround(ph::Q(lmain + 8)));

    if (charge_code == 0) {
      // ------- Neutral path -------
      // No PA.TRAC. 4-momentum from the direct charged-first PA view.
      const auto found = lpaToView.find(lpa);
      const int vecp_i = found == lpaToView.end() ? 0 : found->second;
      if (vecp_i < 1) { mark_pa_skipped(paIdx); return; }
      const auto& view = views[vecp_i];

      auto npfo = pfoCol.create();
      npfo.setMomentum(
          {view.momentum[0], view.momentum[1], view.momentum[2]});
      npfo.setEnergy(view.energy);
      npfo.setMass(view.mass);
      npfo.setCharge(0.f);
      record_particle(npfo, vecp_i, paIdx, lpa);
      // d0PV/z0PV/d0BS are parallel to Tracks (charged-only) and so are not
      // pushed for neutrals; the per-particle words are.
      push_particle_words(vecp_i, lpa, lmain);
      return;
    }

    // ------- Charged path -------
    // PA.TRAC: perigee (d0, z0, theta, phi, 1/R) at +2..+6;
    // 15-element lower-tri weight matrix at +7..+21.
    const int ltrac = lphpa("TRAC", lpa);
    if (ltrac <= 0) { mark_pa_skipped(paIdx); return; }
    const float D0_dp    = ph::Q(ltrac + 2);    // cm
    const float Z0_dp    = ph::Q(ltrac + 3);    // cm
    const float theta_dp = ph::Q(ltrac + 4);    // rad
    const float phi_dp   = ph::Q(ltrac + 5);    // rad
    const float invR_dp  = ph::Q(ltrac + 6);    // 1/cm (signed)
    std::array<float, 15> Wraw{};
    for (int k = 0; k < 15; ++k) Wraw[k] = ph::Q(ltrac + 7 + k);

    const auto helix = Helix::fromPerigee(D0_dp, Z0_dp, theta_dp, phi_dp,
                                          invR_dp, &Wraw);
    if (!helix.valid()) {
      mark_pa_skipped(paIdx);
      return;   // degenerate sin(theta) -> skip
    }

    auto trk = trkCol.create();
    trk.addToTrackStates(helix.toTrackState(edm4hep::TrackState::AtIP));

    // AABTAG measures an impact parameter for the subset of tracks it can
    // use, against its own primary vertex. That is a property of the track,
    // so it rides here as a state at that vertex rather than in a parallel
    // array; a track AABTAG skipped simply has no AtVertex state.
    //
    // D0 is negated into the EDM4hep convention, as the perigee above is
    // (Helix::fromPerigee) -- AABTAG stores the DELPHI sign. Z0 is not
    // negated, matching the same routine. Only these two components are
    // measured; the rest stay NaN rather than zero, which would claim a
    // measurement that was never made.
    if (auto it = lpa_to_btag.find(lpa); it != lpa_to_btag.end()) {
      trk.addToTrackStates(aabtag::vertexState(it->second));
    }

    // Track elements reconstructed from this PA, decoded by
    // TrackElementsWriter. Linked here, while the track is still mutable.
    if (ctx_.track_elements) {
      const auto& te = *ctx_.track_elements;
      if (paIdx < static_cast<int>(te.pa_to_segments.size())) {
        for (const auto& seg : te.pa_to_segments[paIdx]) trk.addToTracks(seg);
      }
      if (paIdx < static_cast<int>(te.pa_to_plane_hits.size())) {
        for (const auto& hit : te.pa_to_plane_hits[paIdx]) {
          trk.addToTrackerHits(hit);
        }
      }
    }

    // Extrapolation states for this PA, decoded by TraxWriter.
    if (ctx_.trax && paIdx < static_cast<int>(ctx_.trax->pa_to_states.size())) {
      for (const auto& st : ctx_.trax->pa_to_states[paIdx]) {
        trk.addToTrackStates(st);
      }
    }

    // chi2 / ndf from PA.MAIN. +26/+27 (with VD) preferred, fallback to
    // +16/+17 (without VD). SKELANA sanitises ndf to [0, 1000].
    float chi2_vd = ph::Q(lmain + 26);
    int   ndf_vd  = static_cast<int>(std::lround(ph::Q(lmain + 27)));
    if (ndf_vd < 0 || ndf_vd > 1000) ndf_vd = 0;
    if (ndf_vd > 0 && chi2_vd > 0.f) {
      trk.setChi2(chi2_vd);
      trk.setNdf(ndf_vd);
    } else {
      float chi2_no_vd = ph::Q(lmain + 16);
      int   ndf_no_vd  = static_cast<int>(std::lround(ph::Q(lmain + 17)));
      if (ndf_no_vd < 0 || ndf_no_vd > 1000) ndf_no_vd = 0;
      if (ndf_no_vd > 0) {
        trk.setChi2(chi2_no_vd);
        trk.setNdf(ndf_no_vd);
      }
    }

    const auto foundView = lpaToView.find(lpa);
    const int vecp_i = foundView == lpaToView.end() ? 0 : foundView->second;

    // VdHitsWriter preserves the bank's raw PA reference. Link by that stable
    // address here while the track is still mutable.
    if (ctx_.vd_hits) {
      const auto found = ctx_.vd_hits->lpa_to_hits.find(lpa);
      if (found != ctx_.vd_hits->lpa_to_hits.end()) {
        for (const auto& hit : found->second) trk.addToTrackerHits(hit);
      }
    }

    // Charge sign from PA.MAIN. Code 3 ("undefined") -> 0 (we preserve
    // the ambiguity rather than mapping to +1 like the current code does).
    int sign = 0;
    if      (charge_code == 1) sign = +1;
    else if (charge_code == 2) sign = -1;
    // else: sign = 0 (undefined)

    float px = 0.f, py = 0.f, pz = 0.f, E = 0.f, mass = 0.f;
    if (vecp_i >= 1) {
      px = views[vecp_i].momentum[0];
      py = views[vecp_i].momentum[1];
      pz = views[vecp_i].momentum[2];
      E = views[vecp_i].energy;
      mass = views[vecp_i].mass;
    }

    auto pfo = pfoCol.create();
    pfo.setMomentum({px, py, pz});
    pfo.setEnergy(E);
    pfo.setMass(mass);
    pfo.setCharge(static_cast<float>(sign));
    pfo.addToTracks(trk);
    record_particle(pfo, vecp_i, paIdx, lpa);

    // LVLOCK: per-VECP track-quality bitmask. -1 sentinel if no VECP
    // match. Stored as int32 so bit 32 (REMCLU overlap) is preserved.
    push_particle_words(vecp_i, lpa, lmain);

    // Direct BS/PV-corrected impacts, in the same charged-first index order.
    push_qtrac_or_nan(vecp_i);
  });

  // Push all collections into the Frame via the base class's put().
  // Handles in `result` remain valid afterwards.
  put(std::move(trkCol),    "TRAC", "Tracks", Provenance::Derived);
  put(std::move(pfoCol),    "MAIN", "Particles", Provenance::Derived);
  put(std::move(lvlockCol), "VECP", "Particles_SelectionFlag", Provenance::Derived);
  put(std::move(codeCol),   "MAIN", "Particles_ReconstructionCode", Provenance::Transcribed);
  put(std::move(detCol),    "MAIN", "Particles_DetectorMask",       Provenance::Transcribed);
  put(std::move(chargeCodeCol), "MAIN", "Particles_ChargeCode",     Provenance::Transcribed);
  put(std::move(lengthCol), "MAIN", "Particles_TrackLength",        Provenance::Transcribed);
  put(std::move(d0PvCol),   "QTRAC", "Tracks_d0PV", Provenance::Derived);
  put(std::move(z0PvCol),   "QTRAC", "Tracks_z0PV", Provenance::Derived);
  put(std::move(d0BsCol),   "QTRAC", "Tracks_d0BS", Provenance::Derived);

  // Hand off to downstream writers via the shared context.
  ctx_.tracking = std::move(result);
}

}  // namespace delphi_edm4hep::tracking
