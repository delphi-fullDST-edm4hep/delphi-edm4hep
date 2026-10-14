// Tracking domain — implementation S3 (pass-1).
//
// Walks the PA chain (LDTOP-1 -> per-PV -> per-PA), emitting:
//   <prefix>_TRAC_Tracks    (Track + TrackState[AtIP] + 5x5 helix-basis cov)
//   <prefix>_MAIN_Particles (charged + neutral; 4-mom from sk::VECP)
//   <prefix>_VECP_Particles_SelectionFlag       (UserData int32)
//   <prefix>_MAIN_Particles_ReconstructionCode  (UserData int32)
//   <prefix>_MAIN_Particles_DetectorMask        (UserData int32)
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
#include "delphi_edm4hep/internal/BeamSpotStatus.h"

#include "delphi_edm4hep/Helix.h"
#include "delphi_edm4hep/internal/PaWalk.h"
#include "skelana/pscvec.hpp"
#include "skelana/psctra.hpp"

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
#include <iostream>
#include <string>

namespace ph = phdst;
namespace sk = skelana;
namespace aa = delphi_edm4hep::aabtag;

namespace {
constexpr double kCm2Mm = 10.0;
constexpr float  kNotMeasured = std::numeric_limits<float>::quiet_NaN();
}  // namespace

namespace delphi_edm4hep::tracking {

namespace {

// The DELPHI perigee -> EDM4hep helix conversion (Jacobian push-forward,
// weight-matrix inversion, TrackState emit) lives in the shared
// delphi_edm4hep::Helix class (Helix.h) so PA.TRAC and PA.ELTR use one
// definition.

// Look up the SKELANA VECP index for a given PA's lpa, using the LVECP
// back-link. The `want_charged` flag gates: a charged-track PA wants
// VECP(7,*) != 0; a neutral PA wants VECP(7,*) == 0. (PSHCTRECOVER
// can reclassify mid-PSCEVT; the gate defends against that.)
// Returns 0 if no match.
int find_vecp_index(int lpa, bool want_charged)
{
  for (int i = sk::LVPART; i <= sk::NVECP; ++i) {
    if (sk::LVECP(i) != lpa) continue;
    const int q = static_cast<int>(std::lround(sk::VECP(7, i)));
    if (want_charged && q != 0) return i;
    if (!want_charged && q == 0) return i;
  }
  return 0;
}

}  // namespace

void TrackingWriter::emit()
{
  // Storage that will be moved into the Frame at the end. Handles taken
  // before the move remain valid (podio guarantees handle stability
  // across the collection move).
  edm4hep::TrackCollection                 trkCol;
  edm4hep::ReconstructedParticleCollection pfoCol;
  podio::UserDataCollection<std::int32_t>  lvlockCol;
  podio::UserDataCollection<std::int32_t>  codeCol;
  podio::UserDataCollection<std::int32_t>  detCol;
  podio::UserDataCollection<float>         lengthCol;
  podio::UserDataCollection<float>         d0PvCol;
  podio::UserDataCollection<float>         z0PvCol;
  podio::UserDataCollection<float>         d0BsCol;

  Output result;
  result.particle_handles.reserve(static_cast<std::size_t>(sk::NVECP + 1));
  // VECP-index map: size sk::NVECP + 1, all -1 initially. Index 0 unused.
  result.vecp_to_particle.assign(static_cast<std::size_t>(sk::NVECP + 1), -1);
  // PA-index map: sized lazily as we walk; -1 entries for PAs we skip.
  // Reserve a reasonable upper bound to avoid mid-walk reallocation.
  result.pa_to_particle.reserve(static_cast<std::size_t>(sk::NVECP + 16));

  // Helper: register a freshly-created particle handle in the output
  // metadata. vecp_i = 0 means "no VECP match" (the handle still goes
  // into particle_handles, but vecp_to_particle is not updated).
  // The current paIdx is taken from the surrounding forEachPA scope.
  auto record_particle = [&](edm4hep::MutableReconstructedParticle pfo,
                              int vecp_i, int paIdx) {
    const int new_idx = static_cast<int>(result.particle_handles.size());
    result.particle_handles.push_back(pfo);
    if (vecp_i >= 1 && vecp_i < static_cast<int>(result.vecp_to_particle.size())) {
      result.vecp_to_particle[vecp_i] = new_idx;
    }
    if (paIdx >= static_cast<int>(result.pa_to_particle.size())) {
      result.pa_to_particle.resize(static_cast<std::size_t>(paIdx + 1), -1);
    }
    result.pa_to_particle[paIdx] = new_idx;
  };

  // Helper: push the BS/PV-corrected impact-parameter triplet for a
  // VECP-matched track, or NaN sentinels if vecp_i is 0.
  //
  // CONVENTION / FOOTGUN (QTRAC_Tracks_d0PV / _z0PV / _d0BS): sk::QTRAC values
  // converted cm -> **mm**, DELPHI sign (rho = Vx sinphi - Vy cosphi), parallel
  // to TRAC_Tracks (charged-only). These are EMPTY (0 / -999) in real DATA --
  // the SKELANA QTRAC common is only filled in MC. For a data-usable impact
  // parameter use PV_Tracks_d0PV (Vertex.cpp): geometric, also **mm** but LCIO
  // sign. Both are filled on MC, where d0 differs by a factor -1 while z0
  // agrees -- do not mix them.
  static constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
  auto push_qtrac_or_nan = [&](int vecp_i) {
    if (vecp_i >= 1) {
      d0PvCol.push_back(sk::QTRAC(38, vecp_i) * 10.f);  // cm -> mm
      z0PvCol.push_back(sk::QTRAC(39, vecp_i) * 10.f);  // cm -> mm
      d0BsCol.push_back(delphi_edm4hep::beamspot::positionUsable(
                            delphi_edm4hep::event::current().beamSpot.errorCode)
                            ? sk::QTRAC(40, vecp_i) * 10.f   // cm -> mm
                            : kNaN);
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

  // Charged PAs whose VECP momentum lookup failed (emitted with zero
  // 4-momentum). Logged once per event below so the degradation is loud.
  int n_zero_mom_charged = 0;

  // Per-particle words running parallel to <tag>_MAIN_Particles.
  //
  //   LVLOCK  selection verdict; 0 = selected. Neutrals are selected too
  //           (PSHSNT, with the per-table calorimeter thresholds), so they
  //           carry a real verdict rather than a sentinel.
  //   code    PXPHOT reconstruction code, IQ(LPA+3) bits 19-25. SKELANA reads
  //           it to reject VD-only tracks (75 with z, 77 without z, VFT-only
  //           from ISVER 107) and ID+VD-only tracks without z (72).
  //   length  track length in cm, the one selection quantity that cannot be
  //           reconstructed from the other emitted collections.
  auto push_particle_words = [&](int vecp_i, int lpa, int lmain) {
    lvlockCol.push_back(vecp_i >= 1 ? sk::LVLOCK(vecp_i) : -1);
    codeCol.push_back((ph::IQ(lpa + 3) >> 18) & 0x7F);
    detCol.push_back(ph::IQ(lpa + 2));
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
      // No PA.TRAC. 4-momentum from VECP. Emit only the Particle.
      const int vecp_i = find_vecp_index(lpa, /*want_charged=*/false);
      if (vecp_i < 1) { mark_pa_skipped(paIdx); return; }

      auto npfo = pfoCol.create();
      npfo.setMomentum({sk::VECP(1, vecp_i),
                        sk::VECP(2, vecp_i),
                        sk::VECP(3, vecp_i)});
      npfo.setEnergy(sk::VECP(4, vecp_i));
      npfo.setMass  (sk::VECP(5, vecp_i));
      npfo.setCharge(0.f);
      record_particle(npfo, vecp_i, paIdx);
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
    // Only D0 and Z0 are measured; the rest stay NaN rather than zero, which
    // would claim a measurement that was never made.
    if (auto it = lpa_to_btag.find(lpa); it != lpa_to_btag.end()) {
      trk.addToTrackStates(
          aabtag::vertexState(it->second, pawalk::trueCharge(lpa)));
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

    // VECP-matched index: required for VECP 4-momentum, LVLOCK, and
    // QTRAC reads. If no match, fall back to perigee-derived momentum
    // (with pion mass) — same fallback the current converter uses.
    const int vecp_i = find_vecp_index(lpa, /*want_charged=*/true);

    // Vertex-Detector hits SKELANA assigned to this track, decoded by
    // VdHitsWriter and grouped by the same charged-track ordinal. Linked
    // here, while the track is still mutable.
    if (ctx_.vd_hits && vecp_i >= 1) {
      const auto& vd = ctx_.vd_hits->vecp_to_hits;
      if (vecp_i < static_cast<int>(vd.size())) {
        for (const auto& hit : vd[static_cast<std::size_t>(vecp_i)]) {
          trk.addToTrackerHits(hit);
        }
      }
    }

    // Charge from PA.MAIN: 1 positive, 2 negative. Code 3 is DELPHI's "charged,
    // sign unknown" (the small-angle tracker); DELPHI never assigns it a sign,
    // so it is NaN here -- never 0, which would read as neutral.
    float charge = 0.f;
    if      (charge_code == 1) charge = +1.f;
    else if (charge_code == 2) charge = -1.f;
    else if (charge_code == 3) charge = kNotMeasured;

    float px = 0.f, py = 0.f, pz = 0.f, E = 0.f, mass = 0.f;
    if (vecp_i >= 1) {
      px   = sk::VECP(1, vecp_i);
      py   = sk::VECP(2, vecp_i);
      pz   = sk::VECP(3, vecp_i);
      E    = sk::VECP(4, vecp_i);
      mass = sk::VECP(5, vecp_i);
    } else {
      // No VECP match -> this charged Particle keeps zero 4-momentum (the
      // perigee-momentum fallback is intentionally not implemented; VECP is
      // authoritative). Count it so the job degrades loudly (logged per event
      // below) instead of silently emitting an unphysical zero-momentum track.
      ++n_zero_mom_charged;
    }

    auto pfo = pfoCol.create();
    pfo.setMomentum({px, py, pz});
    pfo.setEnergy(E);
    pfo.setMass(mass);
    pfo.setCharge(charge);
    pfo.addToTracks(trk);
    record_particle(pfo, vecp_i, paIdx);

    // LVLOCK: per-VECP track-quality bitmask. -1 sentinel if no VECP
    // match. Stored as int32 so bit 32 (REMCLU overlap) is preserved.
    push_particle_words(vecp_i, lpa, lmain);

    // BS/PV-corrected impact parameters from sk::QTRAC(38..40, vecp_i).
    // PSCTRA indexes by the charged-VECP ordinal which matches vecp_i
    // when IFLODR=1 (charged-first ordering).
    push_qtrac_or_nan(vecp_i);
  });

  if (n_zero_mom_charged > 0) {
    std::cerr << "delphi_edm4hep::tracking: WARNING: " << n_zero_mom_charged
              << " charged PFO(s) had no VECP momentum match and were emitted"
              << " with zero 4-momentum in this event\n";
  }

  // Push all collections into the Frame via the base class's put().
  // Handles in `result` remain valid afterwards.
  put(std::move(trkCol),    "TRAC", "Tracks", Provenance::Derived);
  put(std::move(pfoCol),    "MAIN", "Particles", Provenance::Derived);
  put(std::move(lvlockCol), "VECP", "Particles_SelectionFlag", Provenance::Derived);
  put(std::move(codeCol),   "MAIN", "Particles_ReconstructionCode", Provenance::Transcribed);
  put(std::move(detCol),    "MAIN", "Particles_DetectorMask",       Provenance::Transcribed);
  put(std::move(lengthCol), "MAIN", "Particles_TrackLength",        Provenance::Transcribed);
  put(std::move(d0PvCol),   "QTRAC", "Tracks_d0PV", Provenance::Derived);
  put(std::move(z0PvCol),   "QTRAC", "Tracks_z0PV", Provenance::Derived);
  put(std::move(d0BsCol),   "QTRAC", "Tracks_d0BS", Provenance::Derived);

  // Hand off to downstream writers via the shared context.
  ctx_.tracking = std::move(result);
}

}  // namespace delphi_edm4hep::tracking
