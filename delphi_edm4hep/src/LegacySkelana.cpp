#include "delphi_edm4hep/internal/LegacySkelana.h"

#include "delphi_edm4hep/Btag/BtagInfo.h"
#include "delphi_edm4hep/Calorimeter/SticInfo.h"
#include "delphi_edm4hep/Event/EventInfo.h"

#include "phdst/functions.hpp"
#include "skelana/functions.hpp"
#include "skelana/pscbsp.hpp"
#include "skelana/pscbtg.hpp"
#include "skelana/pscemf_hpc_hac_stic.hpp"
#include "skelana/pscevt.hpp"
#include "skelana/pscflg.hpp"
#include "skelana/pscvec.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace sk = skelana;

namespace delphi_edm4hep::legacy_skelana {

void initialize() {
  sk::PSINI();

  // Exact production configuration inherited from delphi-nanoaod. Keep this
  // isolated until the converter-owned particle pipeline replaces it.
  sk::IFLTRA = 1;
  sk::IFLODR = 1;
  sk::IFLVEC = 22;
  sk::IFLSTR = 11;
  sk::IFLCUT = 3;
  sk::IFLRVR = 111;
  sk::IFLSIM = 1;
  sk::IFLBSP = 2;
  sk::IFLBTG = 2;
  sk::IFLPVT = 0;
  sk::IFLVDR = 1;
  sk::IFLFCT = 1;
  sk::IFLRNQ = 0;
  sk::IFLBHP = 1;
  sk::IFLUTE = 1;
  sk::IFLVDH = 1;
  sk::IFLMUO = 1;
  sk::IFLECL = 22;
  sk::IFLELE = 1;
  sk::IFLEMC = 1;
  sk::IFLPHO = 1;
  sk::IFLPHC = 1;
  sk::IFLSTC = 1;
  sk::IFLHAC = 1;
  sk::IFLHAD = 1;
  sk::IFLRV0 = 1;
}

void processRecord() {
  sk::PSBEG();

  // PSBEG has already run PSHEVT and PSBEAM. Preserve those exact values,
  // rather than letting the harness call the direct service afterwards. A
  // second VDBSPT call is not observationally neutral for MC: it draws a new
  // smeared interaction point and advances the package random state.
  event::EventInfo eventInfo;
  eventInfo.processingTag = sk::CDTYPE();
  const auto tagLast = eventInfo.processingTag.find_last_not_of(' ');
  eventInfo.processingTag.erase(tagLast == std::string::npos ? 0 : tagLast + 1);
  eventInfo.dstVersion = sk::ISVER;
  eventInfo.centreOfMassEnergy = sk::ECMAS;
  const auto [tesla, gevPerCm] = phdst::BPILOT();
  eventInfo.magneticFieldTesla = tesla;
  eventInfo.magneticFieldGevPerCm = gevPerCm;
  eventInfo.hadronicTagTeam4 = sk::IHAD4;
  eventInfo.nChargedTeam4 = sk::NCTR4;
  eventInfo.nCharged = sk::NCTRK;
  eventInfo.nNeutral = sk::NNTRK;
  eventInfo.chargedEnergy = sk::ECHAR;
  eventInfo.neutralEmEnergy = sk::EMNEU;
  eventInfo.neutralHadEnergy = sk::EHNEU;
  eventInfo.beamSpot.errorCode = sk::IERRBS;
  for (int i = 1; i <= 3; ++i) {
    eventInfo.beamSpot.positionCm[i - 1] = sk::XYZBS(i);
    eventInfo.beamSpot.sigmaCm[i - 1] = sk::DXYZBS(i);
  }
  event::setLegacySnapshot(std::move(eventInfo));

  btag::EventLevelTag tag;
  tag.probabilityNegative = {sk::QBTPRN(1), sk::QBTPRN(2), sk::QBTPRN(3)};
  tag.probabilityPositive = {sk::QBTPRP(1), sk::QBTPRP(2), sk::QBTPRP(3)};
  tag.probabilityAll = {sk::QBTPRS(1), sk::QBTPRS(2), sk::QBTPRS(3)};
  tag.thrustAxis = {sk::QBTTHR(1), sk::QBTTHR(2), sk::QBTTHR(3)};
  tag.thrustValue = sk::QBTTHR(4);
  btag::setLegacyRecalculated(tag, sk::IERRBS == 0);

  std::vector<stic::Row> rows;
  const int last = std::min(sk::NVECP, sk::MTRACK);
  for (int i = sk::LVPART; i <= last; ++i) {
    bool filled = false;
    for (int k = 1; k <= sk::LENSTC; ++k) {
      filled = filled || sk::KSTIC(k, i) != 0;
    }
    if (!filled) continue;

    stic::Row row;
    row.vecpIndex = i;
    row.measurement = {sk::QSTIC(1, i), sk::QSTIC(2, i), sk::QSTIC(3, i)};
    for (int k = 4; k <= sk::LENSTC; ++k) {
      row.attributes[k - 4] = sk::KSTIC(k, i);
    }
    rows.push_back(row);
  }
  stic::setLegacyRows(std::move(rows));
}

}  // namespace delphi_edm4hep::legacy_skelana
