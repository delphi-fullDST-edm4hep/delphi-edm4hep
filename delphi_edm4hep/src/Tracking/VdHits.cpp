// VdHitsWriter — direct compact shortDST VD-hit decoding.
//
// The MVDH bank at LDTOP-21 contains associated hits first (one PA reference
// downlink per hit), followed by unassociated hits. This is the source used by
// PSHVDH; decoding it here removes PSCVDA, PSCVDU and PSCVEC from this domain.

#include "delphi_edm4hep/Tracking/VdHits.h"

#include "delphi_edm4hep/internal/PaWalk.h"

#include "phdst/uxcom.hpp"
#include "phdst/uxlink.hpp"

#include <edm4hep/MutableTrackerHit3D.h>
#include <edm4hep/TrackerHit3DCollection.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ph = phdst;

namespace delphi_edm4hep::vd_hits {

namespace {

constexpr double kCm2Mm = 10.0;
constexpr int kMaxHitsPerTrack = 12;
constexpr int kMaxUnassociatedHits = 1000;

struct RawHit {
  int module = 0;
  float slot2 = 0.f;
  float radius = 0.f;
  float slot4 = 0.f;
  float signalToNoise = 0.f;
};

RawHit readHit(int bank, int dataOffset, int wordsPerHit) {
  RawHit hit;
  hit.module = std::lround(ph::Q(bank + dataOffset + 1));
  hit.slot2 = ph::Q(bank + dataOffset + 2);
  hit.radius = ph::Q(bank + dataOffset + 3);
  hit.slot4 = ph::Q(bank + dataOffset + 4);
  if (wordsPerHit > 4) hit.signalToNoise = ph::Q(bank + dataOffset + 5);
  return hit;
}

void fillHit(edm4hep::MutableTrackerHit3D hit, const RawHit& raw) {
  hit.setCellID(static_cast<std::uint64_t>(
      static_cast<std::int64_t>(raw.module)));
  hit.setType(raw.radius < 0.f ? 1 : 0);
  hit.setEDep(raw.signalToNoise);
  hit.setPosition({raw.radius * kCm2Mm, raw.slot2 * kCm2Mm,
                   raw.slot4 * kCm2Mm});
}

}  // namespace

void VdHitsWriter::emit() {
  edm4hep::TrackerHit3DCollection pointsCol;
  edm4hep::TrackerHit3DCollection hitsCol;
  Output out;

  if (ph::LDTOP <= 0 || ph::IQ(ph::LDTOP - 2) <= 21) {
    put(std::move(pointsCol), "TDVD", "VDPoints", Provenance::Transcribed);
    put(std::move(hitsCol), "TDVD", "VDHits", Provenance::Transcribed);
    ctx_.vd_hits = std::move(out);
    return;
  }
  const int bank = ph::LQ(ph::LDTOP - 21);
  if (bank <= 0) {
    put(std::move(pointsCol), "TDVD", "VDPoints", Provenance::Transcribed);
    put(std::move(hitsCol), "TDVD", "VDHits", Provenance::Transcribed);
    ctx_.vd_hits = std::move(out);
    return;
  }

  const int descriptor = ph::IQ(bank + 1);
  int totalHits = descriptor % 1000;
  int wordsPerHit = descriptor / 1000;
  if (descriptor == 5000) {
    totalHits = 1000;
    wordsPerHit = 4;
  } else if (descriptor == 6000) {
    totalHits = 1000;
    wordsPerHit = 5;
  }
  if (totalHits < 0 || wordsPerHit < 4) totalHits = 0;
  const int associatedHits =
      std::clamp(ph::IQ(bank - 3), 0, totalHits);

  std::unordered_map<int, std::vector<RawHit>> associatedByPa;
  int dataOffset = 1;
  for (int n = 1; n <= associatedHits; ++n) {
    const int lpa = ph::LQ(bank - n);
    if (lpa > 0) {
      auto& hits = associatedByPa[lpa];
      if (hits.size() < static_cast<std::size_t>(kMaxHitsPerTrack)) {
        hits.push_back(readHit(bank, dataOffset, wordsPerHit));
      }
    }
    dataOffset += wordsPerHit;
  }

  // PSHVDH stores hits per VECP entry and the old writer emitted those groups
  // in charged-PA order. Walking the raw PA chain and selecting only addresses
  // present in the hit bank gives the same stable grouping without PSCVEC.
  pawalk::forEachPA([&](int lpa, int /*paIndex*/) {
    const auto found = associatedByPa.find(lpa);
    if (found == associatedByPa.end()) return;
    auto& outputHits = out.lpa_to_hits[lpa];
    outputHits.reserve(found->second.size());
    for (const auto& raw : found->second) {
      auto hit = hitsCol.create();
      fillHit(hit, raw);
      outputHits.push_back(hit);
    }
  });

  const int unassociated =
      std::min(totalHits - associatedHits, kMaxUnassociatedHits);
  for (int i = 0; i < unassociated; ++i) {
    fillHit(pointsCol.create(), readHit(bank, dataOffset, wordsPerHit));
    dataOffset += wordsPerHit;
  }

  put(std::move(pointsCol), "TDVD", "VDPoints", Provenance::Transcribed);
  put(std::move(hitsCol), "TDVD", "VDHits", Provenance::Transcribed);
  ctx_.vd_hits = std::move(out);
}

}  // namespace delphi_edm4hep::vd_hits
