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

#include "phdst/phciii.hpp"
#include "phdst/uxcom.hpp"      // IQ
#include "phdst/uxlink.hpp"     // LDTOP

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

void checkEventIsUsable(const delphi_edm4hep::event::EventInfo& info) {
  // EBEAM = ECMAS/2 is both a track cut and a denominator in the direct
  // selection and DELPHI tagging services; at zero every charged track fails.
  if (info.centreOfMassEnergy <= 0.f) {
    refuse("centre-of-mass energy is " +
           std::to_string(info.centreOfMassEnergy) + " GeV");
  }

  // VDBSPT returns without setting IERRBS when it cannot open the
  // per-processing .DB file (vdbeam.car:1167-1171), leaving the position at the
  // origin with no error raised, so BeamSpotErrorCode alone cannot be trusted.
  const auto& position = info.beamSpot.positionCm;
  if (position[0] == 0.f && position[1] == 0.f && position[2] == 0.f) {
    refuse("beam spot is exactly (0,0,0); the per-processing .DB lookup did "
           "not produce a position");
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

  // Era identifiers. The processing tag selects DELPHI calibration data — its
  // first two characters pick the RICH refractive index — and the PXDST version
  // selects between algorithm generations.
  stored("dstProcessingTag", info.processingTag);
  stored("pxdstVersion",     ph::LDTOP > 0 ? ph::IQ(ph::LDTOP + 3) : 0);

  // Centre-of-mass energy from the DANA pilot blocklet. Refuse a missing block
  // instead of silently substituting the historical 91.250 GeV default, which
  // would be wrong by more than a factor of two at LEP2.
  stored("ECMS", info.centreOfMassEnergy);

  // Per-event solenoid field and the curvature-to-momentum factor, both from
  // the pilot record. pT [GeV/c] = BFieldGevPerCm / |omega_DELPHI [1/cm]|.
  stored("BField",         info.magneticFieldTesla);
  stored("BFieldGevPerCm", info.magneticFieldGevPerCm);

  // Multiplicities and summed energies counted directly over the PA chain
  // under the historical PSHEVT cuts — a momentum, track-length, impact-
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
  // the DSTQID tag and looked up per run and cartridge — not from the DST.
  // For simulation it is instead the generated interaction point plus a
  // Gaussian smear, so it follows the event rather than describing a beam.
  auto beamspot = parameters("EVT", Provenance::Derived);
  beamspot("BeamSpotX",      info.beamSpot.positionCm[0] * kCm2Mm);
  beamspot("BeamSpotY",      info.beamSpot.positionCm[1] * kCm2Mm);
  beamspot("BeamSpotZ",      info.beamSpot.positionCm[2] * kCm2Mm);
  beamspot("BeamSpotSigmaX", info.beamSpot.sigmaCm[0] * kCm2Mm);
  beamspot("BeamSpotSigmaY", info.beamSpot.sigmaCm[1] * kCm2Mm);
  beamspot("BeamSpotSigmaZ", info.beamSpot.sigmaCm[2] * kCm2Mm);
  beamspot("BeamSpotErrorCode", info.beamSpot.errorCode);
}

}  // namespace delphi_edm4hep::event
