// EventWriter — implementation.
//
// Writes the direct DELPHI event context into the Frame as named parameters
// under "<source>_EVT_*". EventInfo reproduces the required PSHEVT/PSBEAM
// logic without reading SKELANA commons.
//
// The parameters fall into three groups by origin, declared separately below:
// values copied from the DST pilot record, counts or sums reconstructed from
// the PA chain, and the beam spot, which comes from an external database
// rather than from the DST at all.

#include "delphi_edm4hep/Event/Event.h"
#include "delphi_edm4hep/Event/EventInfo.h"
#include "delphi_edm4hep/internal/BeamSpotStatus.h"

#include "phdst/functions.hpp"  // IPHPIC
#include "phdst/phciii.hpp"
#include "phdst/uxcom.hpp"      // IQ
#include "phdst/uxlink.hpp"     // LDTOP

#include <limits>
#include <stdexcept>
#include <string>

namespace ph = phdst;

namespace {
// Refuse the event, naming it. A writer that throws aborts the job through the
// harness's callback guard, which marks the partial output unpublishable.
[[noreturn]] void refuse(const std::string& what) {
  throw std::runtime_error(what + " (run " + std::to_string(ph::IIIRUN)
                           + ", event " + std::to_string(ph::IIIEVT) + ')');
}

// The beam energy has a failure mode SKELANA does not report: it substitutes a
// value and carries on, and the substitute reaches the output looking like a
// measurement. (The beam spot's is handled in internal/BeamSpotStatus.h.)
void checkEventIsUsable(const delphi_edm4hep::event::EventInfo& info) {
  // Without the DANA pilot blocklet the centre-of-mass energy is not read but
  // set to 91.250 GeV (skelana.car:2743-2748). That substitution cannot be
  // recognised from the energy itself — 91.250 is also the true stored value
  // for LEP1 simulation — so test for the blocklet.
  if (ph::IPHPIC("DANA", 0) <= 0) {
    refuse("DANA pilot blocklet absent, so the centre-of-mass energy would be "
           "SKELANA's 91.250 GeV fallback rather than a stored value");
  }

  // EBEAM = ECMAS/2 is both a track cut and a denominator in SKELANA; at zero
  // it rejects every charged track.
  if (info.centreOfMassEnergy <= 0.f) {
    refuse("centre-of-mass energy is " +
           std::to_string(info.centreOfMassEnergy) + " GeV");
  }
}
}  // namespace

namespace delphi_edm4hep::event {

void EventWriter::emit() {
  const auto& info = current();
  checkEventIsUsable(info);

  constexpr float kCm2Mm = 10.0f;

  // Pilot-record words, written when the DST was produced.
  auto stored = parameters("EVT", Provenance::Transcribed);
  stored("runNumber",   ph::IIIRUN);
  stored("eventNumber", ph::IIIEVT);
  stored("fileSeq",     ph::IIFILE);
  stored("date",        ph::IIIDAT);   // yymmdd
  stored("time",        ph::IIITIM);   // hhmmss
  stored("fillNumber",  ph::IIFILL);
  stored("experiment",  ph::IIIEXP);
  stored("dstVersion",  info.dstVersion);

  // Era identifiers. The processing tag selects the calibration SKELANA uses —
  // its first two characters pick the RICH refractive index — and the PXDST
  // version selects between algorithm generations.
  stored("dstProcessingTag", info.processingTag);
  stored("pxdstVersion",     ph::LDTOP > 0 ? ph::IQ(ph::LDTOP + 3) : 0);

  // Centre-of-mass energy from the DANA pilot blocklet. SKELANA substitutes
  // 91.250 GeV when that blocklet is absent, which at LEP2 would be wrong by
  // more than a factor of two; check BeamSpotErrorCode and the energy itself
  // before relying on it.
  stored("ECMS", info.centreOfMassEnergy);

  // Per-event solenoid field and the curvature-to-momentum factor, both from
  // the pilot record. pT [GeV/c] = BFieldGevPerCm / |omega_DELPHI [1/cm]|.
  stored("BField",         info.magneticFieldTesla);
  stored("BFieldGevPerCm", info.magneticFieldGevPerCm);

  // Multiplicities and summed energies counted by SKELANA over the PA chain
  // under cuts hard-coded in PSHEVT — a momentum, track-length, impact-
  // parameter and polar-angle selection independent of IFLCUT and of the
  // LVLOCK track selection. Two events with the same nCharged can therefore
  // disagree with a count made from MAIN_Particles and LVLOCK.
  auto counted = parameters("EVT", Provenance::Derived);
  counted("hadronicTagTeam4", info.hadronicTagTeam4);
  counted("nChargedTeam4",    info.nChargedTeam4);
  counted("nCharged",         info.nCharged);
  counted("nNeutral",         info.nNeutral);
  counted("EChargedTotal",    info.chargedEnergy);
  counted("ENeutralEM",       info.neutralEmEnergy);
  counted("ENeutralHad",      info.neutralHadEnergy);

  // Beam spot from the per-processing database under $DELPHI_DAT, selected by
  // the DSTQID tag and looked up per run and file — not from the DST. For
  // simulation it is instead the generated interaction point plus a Gaussian
  // smear, so it follows the event rather than describing a beam. Without a
  // usable entry (BeamSpotErrorCode 2) position and widths are NaN.
  const bool usable = beamspot::positionUsable(info.beamSpot.errorCode);
  auto bs = [&](float cm) {
    return usable ? cm * kCm2Mm : std::numeric_limits<float>::quiet_NaN();
  };
  auto beamspot = parameters("EVT", Provenance::Derived);
  beamspot("BeamSpotX",      bs(info.beamSpot.positionCm[0]));
  beamspot("BeamSpotY",      bs(info.beamSpot.positionCm[1]));
  beamspot("BeamSpotZ",      bs(info.beamSpot.positionCm[2]));
  beamspot("BeamSpotSigmaX", bs(info.beamSpot.sigmaCm[0]));
  beamspot("BeamSpotSigmaY", bs(info.beamSpot.sigmaCm[1]));
  beamspot("BeamSpotSigmaZ", bs(info.beamSpot.sigmaCm[2]));
  beamspot("BeamSpotErrorCode", info.beamSpot.errorCode);
}

}  // namespace delphi_edm4hep::event
