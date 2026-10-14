#include "delphi_edm4hep/Event/EventInfo.h"

#include "delphi_edm4hep/internal/PaWalk.h"
#include "delphi_edm4hep/internal/PilotRecord.h"

#include "phdst/functions.hpp"
#include "phdst/uxcom.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace ph = phdst;

extern "C" {
  void dstqid_(char* tag, std::size_t len);
  void setbs_(int* mode, int* index, float* sigma);
  void vdbspt_(float* position, float* sigma, int* error);
  void vdidst_();

}

namespace delphi_edm4hep::event {
namespace {

EventInfo info;
bool vdInitialized = false;
bool beamDefaultsInitialized = false;

// Default beam sizes (sigma x, y, z in cm), one row per processing year from
// 1991 to 2002. Copied from SKELANA's PSCBSD deck (SIGBS(3,12) in
// stdcdes.car, dstana 2017-12-01); PSBEAM passes the row for the current
// processing to SETBS(1, 1, ...). From then on VDBSPT adds these sizes in
// quadrature to the widths it reports and, for simulation, draws the beam spot
// from a Gaussian of these sizes about the primary vertex.
constexpr std::array<std::array<float, 3>, 12> beamSigmaDefaults{{
    {0.0157f, 0.0010f, 0.930f}, {0.0102f, 0.0010f, 0.710f},
    {0.0150f, 0.0010f, 0.730f}, {0.0119f, 0.0010f, 0.700f},
    {0.0152f, 0.0010f, 0.720f}, {0.0164f, 0.0010f, 0.760f},
    {0.0160f, 0.0010f, 0.760f}, {0.0160f, 0.0010f, 0.760f},
    {0.0160f, 0.0010f, 0.760f}, {0.0160f, 0.0010f, 0.760f},
    {0.0160f, 0.0010f, 0.760f}, {0.0160f, 0.0010f, 0.760f},
}};

std::string processingTag() {
  char tag[4] = {};
  dstqid_(tag, sizeof(tag));
  std::string result(tag, sizeof(tag));
  const auto last = result.find_last_not_of(' ');
  result.erase(last == std::string::npos ? 0 : last + 1);
  return result;
}

// Row of beamSigmaDefaults for a processing tag. The second character is the
// last digit of the year; tags starting with 'A' are 2000 onwards, as in
// PSBEAM.
int defaultBeamSigmaIndex(const std::string& tag) {
  if (tag.size() < 2 || tag[1] < '0' || tag[1] > '9') {
    throw std::runtime_error("DSTQID returned an invalid processing tag: '" +
                             tag + "'");
  }
  int year = tag[1] - '0' + (tag[0] == 'A' ? 10 : 0);
  // 1990 has no row. PSBEAM reads SIGBS(1,0), outside the table, so DELPHI
  // gives no default here; using the 1991 row is our choice. It matters only
  // for simulation: no 1990 beam-spot database is shipped, so 1990 data is
  // refused by beamspot::requireDatabase() anyway.
  if (year == 0) year = 1;
  if (year < 1 || year > static_cast<int>(beamSigmaDefaults.size())) {
    throw std::runtime_error("DSTQID processing tag has no beam default: '" +
                             tag + "'");
  }
  return year - 1;
}

void refreshBeamSpot(const std::string& tag) {
  if (!beamDefaultsInitialized) {
    auto sigma = beamSigmaDefaults.at(defaultBeamSigmaIndex(tag));
    int mode = 1;
    int index = 1;
    setbs_(&mode, &index, sigma.data());
    beamDefaultsInitialized = true;
  }
  // While SKELANA still runs, PSBEAM has already called VDBSPT for this
  // event. Calling it again returns the same beam spot: VDBSPT keeps the
  // current event's result (VDMCBS for simulation, the same-run path of VDBMCA
  // for data), so it neither repeats the lookup nor draws new random numbers.
  //
  // VDBSPT leaves its outputs untouched when it cannot open the
  // per-processing .DB file. Clear them first, so that case shows up as zero
  // widths (see internal/BeamSpotStatus.h) rather than as the previous event's
  // beam spot.
  info.beamSpot = {};
  vdbspt_(info.beamSpot.positionCm.data(), info.beamSpot.sigmaCm.data(),
          &info.beamSpot.errorCode);
}

void refreshCounts() {
  info.hadronicTagTeam4 = 0;
  info.nChargedTeam4 = 0;
  info.nCharged = 0;
  info.nNeutral = 0;
  info.chargedEnergy = 0.f;
  info.neutralEmEnergy = 0.f;
  info.neutralHadEnergy = 0.f;

  pawalk::forEachPA([](int lpa, int) {
    const int main = pawalk::lphpa("MAIN", lpa);
    if (main <= 0) return;
    if (std::lround(ph::Q(main + 8)) != 0) {
      ++info.nCharged;
      const float momentum = ph::Q(main + 7);
      const float energy = ph::Q(main + 6);
      if (momentum < 0.4f || ph::Q(main + 9) < 30.f ||
          std::abs(ph::Q(main + 11)) > 4.f ||
          std::abs(ph::Q(main + 20)) > 10.f || energy == 0.f ||
          std::abs(ph::Q(main + 19) / energy) > 1.f || momentum == 0.f ||
          std::abs(ph::Q(main + 5) / momentum) > 0.9397f) {
        return;
      }
      ++info.nChargedTeam4;
      info.chargedEnergy += energy;
      return;
    }

    ++info.nNeutral;
    const int type = std::lround(ph::Q(main + 10));
    if (type == 21 || type == 47) {
      info.neutralEmEnergy += ph::Q(main + 6);
    } else if (type != 0) {
      info.neutralHadEnergy += ph::Q(main + 6);
    }
  });

  const int identity = ph::IPHPIC("IDEN", 0);
  if (identity >= 0) {
    const auto flags = static_cast<unsigned>(pilot::word(identity + 6));
    info.hadronicTagTeam4 = (flags & 0x3U) == 0x3U ? 1 : 0;
  } else if (info.nChargedTeam4 >= 5 && info.centreOfMassEnergy > 0.f &&
             info.chargedEnergy / info.centreOfMassEnergy >= 0.12f) {
    info.hadronicTagTeam4 = 1;
  }
}

}  // namespace

void initialize() {
  if (!vdInitialized) {
    vdidst_();
    vdInitialized = true;
  }
}

void refresh() {
  initialize();
  info.processingTag = processingTag();

  const int identity = ph::IPHPIC("IDEN", 1);
  info.dstVersion = identity > 0 ? pilot::word(identity + 4) % 1000 : 0;

  // Without the DANA blocklet there is no stored energy. It is left at 0, and
  // EventWriter refuses the event.
  const int data = ph::IPHPIC("DANA", 0);
  info.centreOfMassEnergy =
      data > 0 ? static_cast<float>(pilot::word(data + 13)) / 1000.f : 0.f;

  const auto [tesla, gevPerCm] = ph::BPILOT();
  info.magneticFieldTesla = tesla;
  info.magneticFieldGevPerCm = gevPerCm;

  refreshBeamSpot(info.processingTag);
  refreshCounts();
}

const EventInfo& current() { return info; }

}  // namespace delphi_edm4hep::event
