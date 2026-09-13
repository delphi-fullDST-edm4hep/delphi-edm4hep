// PidExtrasSdstWriter — direct PA decoding and standalone PID algorithms.

#include "delphi_edm4hep/Pid/PidExtrasSdst.h"

#include "delphi_edm4hep/internal/PaWalk.h"

#include "phdst/uxcom.hpp"
#include "phdst/uxlink.hpp"

#include <edm4hep/MutableParticleID.h>
#include <edm4hep/ParticleIDCollection.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ph = phdst;

namespace delphi_edm4hep::pid_extras_sdst {

extern "C" {
void richid_();
void getmine_(int* type);
void xnewtag_(int* idtan, int* pion, int* kaon, int* proton, int* heavy,
              int* pionTrack, int* kaonTrack, int* protonTrack,
              int* heavyTrack);
void xnewpro_(int* idtan, int* pion, int* kaon, int* proton, int* heavy,
              int* electron, int* selection, float* probabilities);
void getdedx_(int* lpa, float* dedx, float* sigma, int* wires, float* error);
void rprodo_(float* momentum, int* region, float* dedx, float* sigma,
             int* wires, float* errors, float* differences, float* pulls,
             float* probabilities, int* quality, int* pion, int* kaon,
             int* proton, int* heavy, int* electron);
void rprode_(int* lpa, float* momentum, int* region, float* dedx,
             float* sigma, int* wires, float* errors, float* differences,
             float* pulls, float* probabilities, int* quality, int* pion,
             int* kaon, int* proton, int* heavy, int* electron);
void rproco_(float* momentum, float* dedxProbabilities,
             float* richProbabilities, int* region, int* pion, int* kaon,
             int* proton, int* heavy, int* electron);
}

namespace {

constexpr std::int32_t kAlgoDedxVD = 7;
constexpr std::int32_t kAlgoXnewtag = 41;
constexpr std::int32_t kAlgoXnewpro = 42;
constexpr std::int32_t kAlgoRprodo = 43;
constexpr std::int32_t kAlgoRprode = 44;
constexpr std::int32_t kAlgoRproco = 45;
constexpr std::int32_t kAlgoPi0 = 111;
constexpr int kNoTag = -1;
constexpr int kRprodeMinPxdst = 333;

bool pi0FieldIsInt(int field) {
  return field == 5 || field == 6 || field == 20 || field == 21 ||
         field == 23;
}

bool useRprode() {
  return ph::LDTOP > 0 && ph::IQ(ph::LDTOP + 3) >= kRprodeMinPxdst;
}

struct HaidLayout {
  int bank = 0;
  int ion = 0;
  int gas = 0;
  int liquid = 0;
  int vd = 0;
  int ring = 0;
  int quality = 0;
  int offset = 2;
};

HaidLayout haidLayout(int lpa) {
  HaidLayout out;
  out.bank = pawalk::lphpa("HAID", lpa);
  if (out.bank <= 0) return out;
  const int descriptor = std::lround(ph::Q(out.bank + 2));
  out.ion = descriptor % 10;
  out.gas = (descriptor / 10) % 10;
  out.liquid = (descriptor / 100) % 10;
  out.vd = (descriptor / 1000) % 10;
  out.ring = (descriptor / 10000) % 10;
  out.quality = (descriptor / 100000) % 10;
  if (out.ion >= 4) out.offset += 4;
  if (out.ion == 2 || out.ion == 6) out.offset += 2;
  return out;
}

struct DerivedTags {
  std::array<int, 8> angle{};
  std::array<int, 6> rich{};
  std::array<int, 6> dedx{};
  std::array<int, 6> combined{};
};

DerivedTags calculateTags(int lpa, bool haveHaid) {
  DerivedTags out;
  if (!haveHaid) return out;  // historical zero row for a missing HAID module
  out.angle.fill(kNoTag);
  out.rich.fill(kNoTag);
  out.dedx.fill(kNoTag);
  out.combined.fill(kNoTag);

  const int lmain = pawalk::lphpa("MAIN", lpa);
  if (lmain <= 0) return out;
  const float px = ph::Q(lmain + 3);
  const float py = ph::Q(lmain + 4);
  const float pz = ph::Q(lmain + 5);
  float momentum = std::sqrt(px * px + py * py + pz * pz);
  if (momentum <= 0.f) return out;
  int region = std::abs(pz / momentum) > 0.7f ? 2 : 1;
  int idtan = ph::IQ(lpa + 1);

  int inputType = 1;
  getmine_(&inputType);
  xnewtag_(&idtan, &out.angle[0], &out.angle[1], &out.angle[2],
           &out.angle[3], &out.angle[4], &out.angle[5], &out.angle[6],
           &out.angle[7]);

  std::array<float, 5> richProbabilities{};
  xnewpro_(&idtan, &out.rich[0], &out.rich[1], &out.rich[2], &out.rich[3],
           &out.rich[4], &out.rich[5], richProbabilities.data());

  float dedx = 0.f;
  float sigma = 0.f;
  int wires = 0;
  std::array<float, 5> errors{}, differences{}, pulls{}, dedxProbabilities{};
  int dedxQuality = 0;
  if (useRprode()) {
    rprode_(&lpa, &momentum, &region, &dedx, &sigma, &wires, errors.data(),
            differences.data(), pulls.data(), dedxProbabilities.data(),
            &dedxQuality, &out.dedx[0], &out.dedx[1], &out.dedx[2],
            &out.dedx[3], &out.dedx[4]);
  } else {
    float error = 0.f;
    getdedx_(&lpa, &dedx, &sigma, &wires, &error);
    rprodo_(&momentum, &region, &dedx, &sigma, &wires, errors.data(),
            differences.data(), pulls.data(), dedxProbabilities.data(),
            &dedxQuality, &out.dedx[0], &out.dedx[1], &out.dedx[2],
            &out.dedx[3], &out.dedx[4]);
  }
  if (dedxQuality == 2) out.dedx[5] = 1;

  rproco_(&momentum, dedxProbabilities.data(), richProbabilities.data(),
          &region, &out.combined[0], &out.combined[1], &out.combined[2],
          &out.combined[3], &out.combined[4]);
  if (out.rich[5] >= 0) {
    out.combined[5] = out.rich[5];
    if (dedxQuality == 2) out.combined[5] |= 1 << 2;
  } else if (dedxQuality == 2) {
    out.combined[5] = 1 << 2;
  }
  return out;
}

std::array<float, 26> readPi0(int lpa) {
  std::array<float, 26> values{};
  const int bank = pawalk::lphpa("PHOT", lpa);
  if (bank <= 0) return values;
  const int descriptor = std::lround(ph::Q(bank + 2));
  const int photonLength = descriptor % 100;
  const int pi0Length = descriptor / 100;
  int offset = 2;
  if (photonLength > 0) offset += photonLength - 1;
  if (pi0Length <= 0) return values;

  for (int field = 1; field <= 19; ++field) {
    values[field - 1] = ph::Q(bank + offset + field);
  }
  const int packed = std::lround(ph::Q(bank + offset + 20));
  values[19] = static_cast<float>(packed % 100);
  values[20] = static_cast<float>(packed / 100);
  if (pi0Length > 20) {
    values[21] = ph::Q(bank + offset + 23);
    values[22] = static_cast<float>(std::lround(ph::Q(bank + offset + 24)));
    values[23] = ph::Q(bank + offset + 25);
    values[24] = ph::Q(bank + offset + 26);
    values[25] = ph::Q(bank + offset + 27);
  }
  return values;
}

}  // namespace

void PidExtrasSdstWriter::emit() {
  const bool rprode = useRprode();
  edm4hep::ParticleIDCollection xnewtagCol, xnewproCol, dedxCol, rprocoCol,
      dedxVdCol, pi0Col;

  auto putAll = [&] {
    put(std::move(xnewtagCol), "XNEWTAG", "RichTags", Provenance::Derived);
    put(std::move(xnewproCol), "XNEWPRO", "RichTags", Provenance::Derived);
    put(std::move(dedxCol), rprode ? "RPRODE" : "RPRODO", "DedxTags",
        Provenance::Derived);
    put(std::move(rprocoCol), "RPROCO", "CombinedTags", Provenance::Derived);
    put(std::move(dedxVdCol), "HAID", "dEdxVD", Provenance::Transcribed);
    put(std::move(pi0Col), "PHOT", "Pi0ID", Provenance::Transcribed);
  };
  if (!ctx_.tracking) {
    putAll();
    return;
  }
  const auto& tracking = *ctx_.tracking;
  richid_();

  auto makePid = [&](edm4hep::ParticleIDCollection& collection, int particle,
                     std::int32_t algorithm) {
    auto pid = collection.create();
    pid.setAlgorithmType(algorithm);
    pid.setParticle(tracking.particle_handles[particle]);
    return pid;
  };
  auto emitTags = [&](edm4hep::ParticleIDCollection& collection, int particle,
                      std::int32_t algorithm, const auto& tags) {
    if (std::all_of(tags.begin(), tags.end(),
                    [](int value) { return value == kNoTag; })) return;
    auto pid = makePid(collection, particle, algorithm);
    for (const int value : tags) pid.addToParameters(static_cast<float>(value));
  };

  for (std::size_t j = 1; j < tracking.vecp_to_particle.size(); ++j) {
    const int particle = tracking.vecp_to_particle[j];
    if (particle < 0 ||
        particle >= static_cast<int>(tracking.particle_lpas.size())) continue;
    const int lpa = tracking.particle_lpas[particle];
    const int lmain = pawalk::lphpa("MAIN", lpa);
    if (lmain <= 0) continue;
    const bool charged = std::lround(ph::Q(lmain + 8)) != 0;

    if (charged) {
      const HaidLayout layout = haidLayout(lpa);
      const DerivedTags tags = calculateTags(lpa, layout.bank > 0);
      emitTags(xnewtagCol, particle, kAlgoXnewtag, tags.angle);
      emitTags(xnewproCol, particle, kAlgoXnewpro, tags.rich);
      emitTags(dedxCol, particle, rprode ? kAlgoRprode : kAlgoRprodo,
               tags.dedx);
      emitTags(rprocoCol, particle, kAlgoRproco, tags.combined);

      if (layout.bank > 0 && layout.vd != 0) {
        const int vdOffset =
            layout.offset + layout.gas + layout.liquid;
        const float value = ph::Q(layout.bank + vdOffset + 1);
        const int hits = std::lround(ph::Q(layout.bank + vdOffset + 2));
        if (hits > 0) {
          auto pid = makePid(dedxVdCol, particle, kAlgoDedxVD);
          pid.addToParameters(value);
          pid.addToParameters(static_cast<float>(hits));
        }
      }
    }

    const auto pi0 = readPi0(lpa);
    if (pi0[0] > 0.f) {
      auto pid = makePid(pi0Col, particle, kAlgoPi0);
      for (int field = 1; field <= 26; ++field) {
        pid.addToParameters(pi0FieldIsInt(field)
                                ? static_cast<float>(std::lround(pi0[field - 1]))
                                : pi0[field - 1]);
      }
    }
  }

  putAll();
}

}  // namespace delphi_edm4hep::pid_extras_sdst
