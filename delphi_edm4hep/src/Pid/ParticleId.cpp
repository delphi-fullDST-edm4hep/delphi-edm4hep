// ParticleId domain — direct PA-word/module decoding (pass 1).
//
// The writer reads MAIN, MUID and HAID itself and calls the standalone DELPHI
// dE/dx helpers. It therefore no longer consumes PSCDEX, PSCMUD, PSCELD,
// PSCHAD, PSCGRC, PSCLRC or PSCVEC.

#include "delphi_edm4hep/Pid/ParticleId.h"

#include "delphi_edm4hep/Event/EventInfo.h"
#include "delphi_edm4hep/internal/PaWalk.h"

#include "phdst/uxcom.hpp"
#include "phdst/uxlink.hpp"

#include <edm4hep/MutableParticleID.h>
#include <edm4hep/ParticleIDCollection.h>
#include <edm4hep/Quantity.h>
#include <edm4hep/RecDqdxCollection.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace ph = phdst;

namespace delphi_edm4hep::particleid {

extern "C" {
void getdedx_(int* lpa, float* dedx, float* sigma, int* nwir, float* error);
void bbdxget_(int* lpa, float* dedx, float* gap, int* nwir, int* method);
float bbdxer_(float* dedx, float* gap, int* nwir, int* method, int* quality);
}

namespace {

constexpr std::int32_t kAlgoDedx = 1;
constexpr std::int32_t kAlgoMuon = 2;
constexpr std::int32_t kAlgoElectron = 3;
constexpr std::int32_t kAlgoHadronID = 4;
constexpr int kBbdxMinPxdst = 333;

int bitField(int word, int first, int length) {
  const auto bits = static_cast<std::uint32_t>(word);
  const auto mask = (std::uint32_t{1} << length) - 1U;
  return static_cast<int>((bits >> (first - 1)) & mask);
}

bool useBbdx() {
  return ph::LDTOP > 0 && ph::IQ(ph::LDTOP + 3) >= kBbdxMinPxdst;
}

const char* dedxAlgo() { return useBbdx() ? "BBDXGET" : "GETDEDX"; }

struct DedxData {
  float value = 0.f;
  float error = 0.f;
  int wires = 0;
  int quality = -1;
};

DedxData readDedx(int lpa) {
  DedxData out;
  if (!useBbdx()) {
    float sigma = 0.f;
    getdedx_(&lpa, &out.value, &sigma, &out.wires, &out.error);
    return out;
  }

  float gap = 0.f;
  int rawWires = 0;
  int method = 0;
  bbdxget_(&lpa, &out.value, &gap, &rawWires, &method);
  float calculatedError = -1.f;
  if (rawWires >= 1) {
    calculatedError =
        bbdxer_(&out.value, &gap, &rawWires, &method, &out.quality);
  }
  // PSHHAD stored no uncertainty for a one-wire measurement, while the
  // converter's historical quality companion still evaluated BBDXER.
  out.error = rawWires > 1 ? calculatedError : -1.f;
  out.wires = std::lround(rawWires * 0.8);
  return out;
}

struct HadronData {
  std::array<float, 18> values{};
};

HadronData readHadron(int lpa) {
  HadronData out;
  const int lhaid = pawalk::lphpa("HAID", lpa);
  if (lhaid <= 0) return out;

  const int descriptor = std::lround(ph::Q(lhaid + 2));
  const int ion = descriptor % 10;
  const int gas = (descriptor / 10) % 10;
  const int liquid = (descriptor / 100) % 10;
  const int vd = (descriptor / 1000) % 10;
  const int ring = (descriptor / 10000) % 10;
  const int quality = (descriptor / 100000) % 10;
  const int tpc = descriptor / 1000000;
  (void)vd;
  (void)tpc;
  int offset = 2;

  if (ion >= 4) offset += 4;
  // A present HAID module starts the signature fields at the SKELANA -1
  // sentinel; missing sub-records retain it.
  for (int i = 0; i <= 7; ++i) out.values[i] = -1.f;
  if (ion == 2 || ion == 6) {
    const int had1 = std::lround(ph::Q(lhaid + offset + 1));
    const int had2 = std::lround(ph::Q(lhaid + offset + 2) * 10.f);
    out.values[0] = static_cast<float>(bitField(ph::IQ(lpa + 3), 13, 3) - 1);
    out.values[1] = static_cast<float>(bitField(ph::IQ(lpa + 3), 16, 3) - 1);
    out.values[2] = static_cast<float>(bitField(had1, 11, 3) - 1);
    out.values[3] = static_cast<float>(bitField(had1, 1, 3) - 1);
    out.values[4] = static_cast<float>(bitField(had1, 4, 3) - 1);
    out.values[5] = static_cast<float>((had2 % 100) - 10) / 10.f;
    out.values[6] = static_cast<float>((had2 / 100) - 10) / 10.f;
    offset += 2;
  }

  const auto richExpected = [](float packed, int dstVersion) {
    if (dstVersion <= 102) return std::lround(packed / 500.f);
    return std::lround(std::trunc(packed / 500.f) / 10.f);
  };
  const int dstVersion = event::current().dstVersion;
  if (gas != 0) {
    const float packed = ph::Q(lhaid + offset + 3);
    out.values[8] = ph::Q(lhaid + offset + 1);
    out.values[9] = ph::Q(lhaid + offset + 2);
    out.values[10] = static_cast<float>(std::lround(packed) % 500);
    out.values[11] = static_cast<float>(richExpected(packed, dstVersion));
    out.values[12] = static_cast<float>(std::lround(ph::Q(lhaid + offset + 4)));
    offset += gas;
  }
  if (liquid != 0) {
    const float packed = ph::Q(lhaid + offset + 3);
    out.values[13] = ph::Q(lhaid + offset + 1);
    out.values[14] = ph::Q(lhaid + offset + 2);
    out.values[15] = static_cast<float>(std::lround(packed) % 500);
    out.values[16] = static_cast<float>(richExpected(packed, dstVersion));
    out.values[17] = static_cast<float>(std::lround(ph::Q(lhaid + offset + 4)));
    offset += liquid;
  }
  offset += vd + ring;
  if (quality != 0) out.values[7] = std::lround(ph::Q(lhaid + offset + 1));
  return out;
}

}  // namespace

void ParticleIdWriter::emit() {
  edm4hep::ParticleIDCollection dedxCol;
  edm4hep::RecDqdxCollection dqdxCol;
  edm4hep::ParticleIDCollection muonCol;
  edm4hep::ParticleIDCollection elecCol;
  edm4hep::ParticleIDCollection hadrCol;

  auto putAll = [&] {
    put(std::move(dedxCol), dedxAlgo(), "Dedx", Provenance::Derived);
    put(std::move(dqdxCol), dedxAlgo(), "DedxRecDqdx", Provenance::Derived);
    put(std::move(muonCol), "MUID", "MuonID", Provenance::Transcribed);
    put(std::move(elecCol), "ELID", "ElectronID", Provenance::Transcribed);
    put(std::move(hadrCol), "HAID", "HadronID", Provenance::Transcribed);
  };
  if (!ctx_.tracking) {
    putAll();
    return;
  }
  const auto& tracking = *ctx_.tracking;

  auto makePid = [&](edm4hep::ParticleIDCollection& collection, int particle,
                     std::int32_t algorithm) {
    auto pid = collection.create();
    pid.setAlgorithmType(algorithm);
    pid.setParticle(tracking.particle_handles[particle]);
    return pid;
  };

  // The current VECP order is retained while Tracking is being ported. Raw PA
  // addresses, rather than any PID common, supply all per-particle inputs.
  for (std::size_t j = 1; j < tracking.vecp_to_particle.size(); ++j) {
    const int particle = tracking.vecp_to_particle[j];
    if (particle < 0 ||
        particle >= static_cast<int>(tracking.particle_lpas.size())) continue;
    const int lpa = tracking.particle_lpas[particle];
    const int lmain = pawalk::lphpa("MAIN", lpa);
    if (lmain <= 0 || std::lround(ph::Q(lmain + 8)) == 0) continue;

    // PSHHAD only invoked the dE/dx helper for PAs carrying a HAID module.
    // Retain that presence gate while decoding the measurement directly.
    const bool hasHaid = pawalk::lphpa("HAID", lpa) > 0;
    const DedxData dedx = hasHaid ? readDedx(lpa) : DedxData{};
    if (hasHaid && (dedx.value > 0.f || dedx.wires > 0)) {
      auto pid = makePid(dedxCol, particle, kAlgoDedx);
      pid.addToParameters(dedx.value);
      pid.addToParameters(dedx.error);
      pid.addToParameters(static_cast<float>(dedx.quality));

      auto dq = dqdxCol.create();
      edm4hep::Quantity quantity;
      quantity.type = 1;
      quantity.value = dedx.value;
      quantity.error = dedx.error;
      dq.setDQdx(quantity);
      for (const auto& track : tracking.particle_handles[particle].getTracks()) {
        dq.setTrack(track);
        break;
      }
    }

    const int muonTag = bitField(ph::IQ(lpa + 3), 1, 5);
    if (muonTag != 0) {
      float chi2 = 0.f;
      int hitPattern = 0;
      const int muid = pawalk::lphpa("MUID", lpa);
      if (muid > 0 && bitField(std::lround(ph::Q(muid + 2)), 1, 1) != 0) {
        chi2 = ph::Q(muid + 4);
        hitPattern = std::lround(ph::Q(muid + 5));
      }
      auto pid = makePid(muonCol, particle, kAlgoMuon);
      pid.addToParameters(static_cast<float>(muonTag));
      pid.addToParameters(chi2);
      pid.addToParameters(static_cast<float>(hitPattern));
    }

    const int electronTag = bitField(ph::IQ(lpa + 3), 7, 3);
    if (electronTag != 0) {
      auto pid = makePid(elecCol, particle, kAlgoElectron);
      pid.addToParameters(static_cast<float>(electronTag));
      pid.addToParameters(static_cast<float>(bitField(ph::IQ(lpa + 3), 10, 3)));
    }

    auto pid = makePid(hadrCol, particle, kAlgoHadronID);
    for (const float value : readHadron(lpa).values) pid.addToParameters(value);
  }

  putAll();
}

}  // namespace delphi_edm4hep::particleid
