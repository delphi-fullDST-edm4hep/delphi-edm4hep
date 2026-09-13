#include "delphi_edm4hep/Calorimeter/SticInfo.h"

#include "delphi_edm4hep/Event/EventInfo.h"

#include "delphi_edm4hep/internal/PaWalk.h"

#include "phdst/uxcom.hpp"

#include <cmath>
#include <cstdint>
#include <utility>

extern "C" {
void sdveto_(int* shower, int* largeVeto, int* combinedVeto);
void vedecode_(int* sideA, int* sideC);
float vmod_(float* values, int* count);
}

namespace delphi_edm4hep::stic {
namespace {

std::vector<Row> rows;

// PXCONS.PI in the shipped DELPHI release. It predates the correctly rounded
// C++ float value and is one ULP smaller, which is observable in wrapped SSTC
// azimuths and sector assignment at boundaries.
constexpr float legacyPi = 0x1.921fb4p+1f;

float mainMomentum(int lmain) {
  int count = 3;
  return vmod_(&phdst::Q(lmain + 3), &count);
}

float positivePhi(float y, float x) {
  const float raw = std::atan2(y, x);
  float phi = raw;
  if (phi < 0.f) {
    phi += 2.f * legacyPi;
  }
  return phi;
}

bool bit(int word, int oneBased) {
  return (static_cast<std::uint32_t>(word) &
          (std::uint32_t{1} << (oneBased - 1))) != 0;
}

bool decodeFullRow(int lpa, Row& row) {
  const int bank = pawalk::lphpa("STIC", lpa);
  if (bank <= 0 || std::lround(phdst::Q(bank + 2)) <= 0) return false;

  // PSFSTC does not advance LSHOWR in its shower loop, so the first shower
  // is also the final common-block value when NSHOWR is greater than one.
  int shower = bank + 3;
  row.lpa = lpa;
  row.measurement = {
      phdst::Q(shower + 5),
      std::atan2(phdst::Q(shower + 6), phdst::Q(shower + 8)),
      phdst::Q(shower + 7) / phdst::Q(shower + 6)};
  row.attributes[0] = std::lround(phdst::Q(shower + 3) / 1000.f);
  sdveto_(&shower, &row.attributes[1], &row.attributes[2]);
  vedecode_(&row.attributes[3], &row.attributes[4]);
  row.attributes[5] = 0;
  return true;
}

bool decodeShortRow(int lpa, Row& row) {
  if (decodeFullRow(lpa, row)) return true;
  const int bank = pawalk::lphpa("SSTC", lpa);
  const int lmain = pawalk::lphpa("MAIN", lpa);
  if (bank <= 0 || lmain <= 0) return false;

  row.lpa = lpa;
  row.measurement[0] = phdst::Q(lmain + 6);
  const float momentum = mainMomentum(lmain);
  if (momentum > 0.f) {
    row.measurement[1] = std::acos(phdst::Q(lmain + 5) / momentum);
    row.measurement[2] = positivePhi(phdst::Q(lmain + 4), phdst::Q(lmain + 3));
  }

  const int shower = bank + 2;
  row.attributes[0] = std::lround(phdst::Q(shower + 2) / 10.f);
  if (event::current().dstVersion <= 103) return true;

  const int hits = row.attributes[0];
  const int veto = shower + 2 + hits;
  const int word1 = std::lround(phdst::Q(veto + 1));
  const int word2 = std::lround(phdst::Q(veto + 2));
  const int word3 = std::lround(phdst::Q(veto + 3));
  const int left1 = word1 / 1000;
  const int centre1 = word1 % 1000;
  const int right1 = word2 / 1000;
  const int left2 = word2 % 1000;
  const int centre2 = word3 / 1000;
  const int right2 = word3 % 1000;

  int front = (centre1 > 50 || centre2 > 50) ? 1 : 0;
  int neighbour = (left1 > 50 || left2 > 50 ||
                   right1 > 50 || right2 > 50) ? 1 : 0;
  if (centre1 > 50 && centre2 > 50) front = 2;
  if ((left1 > 50 && left2 > 50) || (right1 > 50 && right2 > 50)) {
    neighbour = 2;
  }

  const int discrimination1 = std::lround(phdst::Q(veto + 4));
  const int discrimination2 = std::lround(phdst::Q(veto + 5));
  const int arm = row.measurement[1] <= legacyPi / 2.f ? 2 : 1;
  int sector = static_cast<int>((row.measurement[2] / legacyPi * 180.f + 22.5f) /
                                22.5f);
  if (arm == 1) {
    sector = 9 - sector;
    if (sector <= 0) sector += 16;
  }
  const int leftSector = sector == 1 ? 16 : sector - 1;
  const int rightSector = sector == 16 ? 1 : sector + 1;
  int discFront = (bit(discrimination1, sector) ||
                   bit(discrimination2, sector)) ? 1 : 0;
  int discNeighbour =
      (bit(discrimination1, leftSector) || bit(discrimination2, leftSector) ||
       bit(discrimination1, rightSector) || bit(discrimination2, rightSector))
          ? 1 : 0;
  if (bit(discrimination1, sector) && bit(discrimination2, sector)) {
    discFront = 2;
  }
  if ((bit(discrimination1, leftSector) && bit(discrimination2, leftSector)) ||
      (bit(discrimination1, rightSector) && bit(discrimination2, rightSector))) {
    discNeighbour = 2;
  }
  front = std::max(front, discFront);
  neighbour = std::max(neighbour, discNeighbour);
  row.attributes[1] = (front == 0 && neighbour == 0)
                          ? -2
                          : ((front <= 1 && neighbour <= 1) ? -1 : 1);
  return true;
}

}  // namespace

void refreshFromFullDst() {
  rows.clear();
  pawalk::forEachPA([](int lpa, int) {
    Row row;
    if (decodeFullRow(lpa, row)) rows.push_back(row);
  });
}

void refreshFromSdst() {
  rows.clear();
  std::vector<Row> chargedRows;
  std::vector<Row> neutralRows;
  pawalk::forEachPA([&](int lpa, int) {
    const int lmain = pawalk::lphpa("MAIN", lpa);
    if (lmain <= 0) return;
    const bool isCharged = std::lround(phdst::Q(lmain + 8)) != 0;
    Row row;
    if (!decodeShortRow(lpa, row)) return;
    (isCharged ? chargedRows : neutralRows).push_back(row);
  });
  // Match IFLODR=1: output rows follow VECP's charged-first ordering even
  // when the underlying PV/PA structure interleaves charged and neutral PAs.
  rows.reserve(chargedRows.size() + neutralRows.size());
  rows.insert(rows.end(), chargedRows.begin(), chargedRows.end());
  rows.insert(rows.end(), neutralRows.begin(), neutralRows.end());
}

void setLegacyRows(std::vector<Row> value) { rows = std::move(value); }

const std::vector<Row>& current() { return rows; }

}  // namespace delphi_edm4hep::stic
