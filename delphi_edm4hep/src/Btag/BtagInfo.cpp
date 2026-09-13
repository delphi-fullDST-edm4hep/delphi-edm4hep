#include "delphi_edm4hep/Btag/BtagInfo.h"

#include "delphi_edm4hep/Event/EventInfo.h"
#include "delphi_edm4hep/internal/AabtagCommons.h"
#include "delphi_edm4hep/internal/PaWalk.h"
#include "delphi_edm4hep/internal/PilotRecord.h"

#include "phdst/functions.hpp"
#include "phdst/uxcom.hpp"
#include "phdst/uxlink.hpp"

#include <algorithm>
#include <array>

namespace ph = phdst;
namespace aa = delphi_edm4hep::aabtag;

namespace delphi_edm4hep::btag {
namespace {

BtagInfo info;
bool recalculationInitialized = false;

EventLevelTag readStoredTag() {
  EventLevelTag tag;
  if (event::current().dstVersion < 102) {
    const int identity = ph::IPHPIC("IDEN", 0);
    if (identity < 0) return tag;
    constexpr float kPackedProbability = 100000.f;
    tag.probabilityNegative = {
        static_cast<float>(pilot::word(identity + 14)) / kPackedProbability,
        static_cast<float>(pilot::word(identity + 15)) / kPackedProbability,
        static_cast<float>(pilot::word(identity + 11)) / kPackedProbability};
    tag.probabilityPositive = {
        static_cast<float>(pilot::word(identity + 16)) / kPackedProbability,
        static_cast<float>(pilot::word(identity + 17)) / kPackedProbability,
        static_cast<float>(pilot::word(identity + 12)) / kPackedProbability};
    tag.probabilityAll = {
        static_cast<float>(pilot::word(identity + 18)) / kPackedProbability,
        static_cast<float>(pilot::word(identity + 19)) / kPackedProbability,
        static_cast<float>(pilot::word(identity + 13)) / kPackedProbability};
    tag.thrustAxis = {
        static_cast<float>(pilot::word(identity + 20)) / kPackedProbability,
        static_cast<float>(pilot::word(identity + 21)) / kPackedProbability,
        static_cast<float>(pilot::word(identity + 22)) / kPackedProbability};
    return tag;
  }

  if (ph::LDTOP <= 0) return tag;

  const int bank = ph::LQ(ph::LDTOP - 25);
  if (bank <= 0 || ph::IQ(bank - 1) <= 6) return tag;

  tag.probabilityNegative = {ph::Q(bank + 21), ph::Q(bank + 22),
                             ph::Q(bank + 18)};
  tag.probabilityPositive = {ph::Q(bank + 23), ph::Q(bank + 24),
                             ph::Q(bank + 19)};
  tag.probabilityAll = {ph::Q(bank + 25), ph::Q(bank + 26),
                        ph::Q(bank + 20)};
  tag.thrustAxis = {ph::Q(bank + 27), ph::Q(bank + 28), ph::Q(bank + 29)};
  tag.thrustValue = ph::Q(bank + 30);
  return tag;
}

}  // namespace

void initialize() {
  // PSINI supplied this DATA default before PSFBTG. AADATA may overwrite it,
  // but the caller-visible processing name is restored below, exactly as in
  // PSFBTG. Fortran CHARACTER*4 is blank-padded and has no terminator.
  info = {};
  std::copy_n("NDEF", 4, aa::aaparm_.namdst);
  recalculationInitialized = false;
}

void refresh() { info.stored = readStoredTag(); }

void recalculate() {
  if (!recalculationInitialized) {
    recalculationInitialized = true;
    const std::array<char, 4> name{{aa::aaparm_.namdst[0],
                                    aa::aaparm_.namdst[1],
                                    aa::aaparm_.namdst[2],
                                    aa::aaparm_.namdst[3]}};
    aa::aadata_();
    std::copy(name.begin(), name.end(), aa::aaparm_.namdst);
    aa::IFK0ST() = 1;
    aa::IFRFIX() = 1;
  }

  info.recalculated = {};
  info.algorithmInvoked = event::current().beamSpot.errorCode == 0;
  if (!info.algorithmInvoked) return;

  auto& tag = info.recalculated;
  const auto& beam = event::current().beamSpot;
  // PSHEVT did this before PSFBTG. AABTGS calls BPILOT/EPILOT itself, but
  // parts of the package still read the historical PXCONS value directly.
  aa::EBEAM() = event::current().centreOfMassEnergy / 2.f;
  auto position = beam.positionCm;
  auto sigma = beam.sigmaCm;
  aa::aabtgs_(position.data(), sigma.data(),
              &tag.probabilityNegative[2], &tag.probabilityPositive[2],
              &tag.probabilityAll[2]);
  aa::aahemi_(tag.probabilityNegative.data(), tag.probabilityPositive.data(),
              tag.probabilityAll.data(), tag.thrustAxis.data());
  tag.thrustValue = aa::THRVAL();

  // Preserve PSFBTG's only intentional PA mutation: publish AABTAG's VD-hit
  // chi2 back into MAIN word 18 for packages that historically consume it.
  pawalk::forEachPA([](int lpa, int) {
    const int main = pawalk::lphpa("MAIN", lpa);
    if (main <= 0) return;
    for (int i = 1; i <= aa::NTRK(); ++i) {
      if (aa::IADTR(i) == lpa) {
        ph::Q(main + 18) = aa::CHI2VD(i);
        break;
      }
    }
  });
}

void setLegacyRecalculated(const EventLevelTag& tag, bool algorithmInvoked) {
  info.recalculated = tag;
  info.algorithmInvoked = algorithmInvoked;
}

const BtagInfo& current() { return info; }

}  // namespace delphi_edm4hep::btag
