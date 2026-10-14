#pragma once

#include <array>
#include <string>

namespace delphi_edm4hep::event {

struct BeamSpot {
  std::array<float, 3> positionCm{};
  std::array<float, 3> sigmaCm{};
  int errorCode = 0;
};

struct EventInfo {
  std::string processingTag;
  int dstVersion = 0;
  float centreOfMassEnergy = 0.f;
  float magneticFieldTesla = 0.f;
  float magneticFieldGevPerCm = 0.f;

  int hadronicTagTeam4 = 0;
  int nChargedTeam4 = 0;
  int nCharged = 0;
  int nNeutral = 0;
  float chargedEnergy = 0.f;
  float neutralEmEnergy = 0.f;
  float neutralHadEnergy = 0.f;

  BeamSpot beamSpot;
};

// Initialise the VD package once per process. PHDST and the DELPHI packages
// are process-global, so this service deliberately has the same lifetime.
void initialize();

// Refresh all values directly from the current PHDST record and DELPHI
// packages. Must be called after PHDST has loaded a DST record.
void refresh();

const EventInfo& current();

}  // namespace delphi_edm4hep::event
