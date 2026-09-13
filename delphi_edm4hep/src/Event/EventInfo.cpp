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
#include <utility>

namespace ph = phdst;

extern "C" {
  void dstqid_(char* tag, std::size_t len);
  void setbs_(int* mode, int* index, float* sigma);
  void vdbspt_(float* position, float* sigma, int* error);
  void vdidst_();
  void makemod8_(int* lpa, int* refit, int* error);

}

namespace delphi_edm4hep::event {
namespace {

EventInfo info;
bool vdInitialized = false;
bool beamDefaultsInitialized = false;
std::array<float, 3> configuredBeamSigma{};
int configuredBeamMode = 1;
int configuredBeamIndex = 1;
int module8Particle = 0;
int module8Refit = 0;
int module8Error = 0;

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

int defaultBeamSigmaIndex(const std::string& tag) {
  if (tag.size() < 2 || tag[1] < '0' || tag[1] > '9') {
    throw std::runtime_error("DSTQID returned an invalid processing tag: '" +
                             tag + "'");
  }
  const int year = tag[1] - '0' + (!tag.empty() && tag[0] == 'A' ? 10 : 0);
  if (year < 1 || year > static_cast<int>(beamSigmaDefaults.size())) {
    throw std::runtime_error("DSTQID processing tag has no beam default: '" +
                             tag + "'");
  }
  return year - 1;
}

void refreshBeamSpot(const std::string& tag) {
  if (!beamDefaultsInitialized) {
    // SETBS registers the array with the legacy bank package, whose LOCB
    // address check requires persistent data-segment storage (not a stack
    // temporary) and may retain the address after this call.
    configuredBeamSigma = beamSigmaDefaults.at(defaultBeamSigmaIndex(tag));
    setbs_(&configuredBeamMode, &configuredBeamIndex,
           configuredBeamSigma.data());
    beamDefaultsInitialized = true;
  }
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

  const int data = ph::IPHPIC("DANA", 0);
  if (data <= 0) {
    throw std::runtime_error("DANA pilot blocklet is absent");
  }
  info.centreOfMassEnergy = static_cast<float>(pilot::word(data + 13)) / 1000.f;

  const auto [tesla, gevPerCm] = ph::BPILOT();
  info.magneticFieldTesla = tesla;
  info.magneticFieldGevPerCm = gevPerCm;

  refreshBeamSpot(info.processingTag);
  refreshCounts();
}

void repairSecondaryHadronicInteractions() {
  pawalk::forEachPA([](int lpa, int) {
    const int reconstructionCode =
        (static_cast<unsigned>(ph::IQ(lpa + 3)) >> 18) & 0x7fU;
    if (reconstructionCode != 120) return;
    // MAKEMOD8 also feeds its argument through LOCF. The original Fortran
    // caller's locals were static; mirror that storage class here.
    module8Particle = lpa;
    module8Refit = 0;  // Fortran LOGICAL .FALSE.
    module8Error = 0;
    makemod8_(&module8Particle, &module8Refit, &module8Error);
  });
}

void setLegacySnapshot(EventInfo snapshot) { info = std::move(snapshot); }

const EventInfo& current() { return info; }

}  // namespace delphi_edm4hep::event
