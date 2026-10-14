#include "delphi_edm4hep/Btag/BtagInfo.h"

#include "delphi_edm4hep/Event/EventInfo.h"
#include "delphi_edm4hep/internal/PilotRecord.h"

#include "phdst/functions.hpp"
#include "phdst/uxcom.hpp"
#include "phdst/uxlink.hpp"

namespace ph = phdst;

namespace delphi_edm4hep::btag {
namespace {

BtagInfo info;

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

void refresh() { info.stored = readStoredTag(); }

const BtagInfo& current() { return info; }

}  // namespace delphi_edm4hep::btag
