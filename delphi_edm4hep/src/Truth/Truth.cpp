// Truth domain — direct DELPHI simulation-bank decoding.
//
// The compact shortDST PVS/STSH structure and the fullDST PV/ST/SH chains are
// decoded here into a small converter-owned LU-like record. The PA -> truth
// correspondence is derived from the same raw links, replacing PSHLUJ,
// PSFLUJ, PSHSIM, PSFSIM, PSCLUJ and PSCTBL.

#include "delphi_edm4hep/Truth/Truth.h"

#include "phdst/uxcom.hpp"
#include "phdst/uxlink.hpp"

#include <edm4hep/MCParticleCollection.h>
#include <edm4hep/RecoMCParticleLinkCollection.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

// JETSET's exhaustive PDG charge table. LUCHGE returns charge in units e/3.
extern "C" int luchge_(int* kf);

namespace ph = phdst;

namespace delphi_edm4hep::truth {

namespace {

constexpr int kMaxTruthParticles = 4000;
constexpr int kMaxInitialParticles = 20;

struct TruthParticle {
  int status = 0;
  int pdg = 0;
  int parent = 0;  // one-based LU-like index; zero means no parent
  std::array<float, 3> momentum{};
  float mass = 0.f;
  std::array<float, 3> vertex{};  // millimetres in the DELPHI contract
};

struct DecodedTruth {
  std::vector<TruthParticle> particles;
  // Raw reconstructed PA address -> zero-based index in particles.
  std::unordered_map<int, int> lpaToGen;
};

struct CompactSimulationBanks {
  int pvs = 0;
  int stsh = 0;
};

int bitField(int word, int first, int length) {
  const auto bits = static_cast<std::uint32_t>(word);
  const auto mask = (std::uint32_t{1} << length) - 1U;
  return static_cast<int>((bits >> (first - 1)) & mask);
}

inline float charge_from_pdg(int pdg) {
  return static_cast<float>(luchge_(&pdg)) / 3.0f;
}

std::optional<CompactSimulationBanks> compactSimulationBanks() {
  if (ph::LDTOP <= 0) return std::nullopt;
  const int links = ph::IQ(ph::LDTOP - 2);
  if (links > 28) {
    const int pvs = ph::LQ(ph::LDTOP - 28);
    const int stsh = ph::LQ(ph::LDTOP - 29);
    if (pvs > 0 && stsh > 0) return CompactSimulationBanks{pvs, stsh};
  }
  // ISVER <= 101 used the older compact link slots. Testing the bank pair is
  // sufficient here and avoids importing SKELANA's event-version common.
  if (links > 18) {
    const int pvs = ph::LQ(ph::LDTOP - 18);
    const int stsh = ph::LQ(ph::LDTOP - 19);
    if (pvs > 0 && stsh > 0) return CompactSimulationBanks{pvs, stsh};
  }
  return std::nullopt;
}

// Direct C++ port of PSSORT. `mother` and `sister` are one-based compact-SH
// indices. The returned vector maps LU-like order to compact-SH order.
std::optional<std::vector<int>> topologicalOrder(
    const std::vector<int>& mother, std::vector<int> sister) {
  const int n = static_cast<int>(mother.size()) - 1;
  if (n <= 0) return std::vector<int>{};

  std::vector<int> initial;
  for (int i = 1; i <= n; ++i) {
    if (mother[i] == 0) initial.push_back(i);
  }
  if (initial.empty() ||
      initial.size() > static_cast<std::size_t>(kMaxInitialParticles)) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i + 1 < initial.size(); ++i) {
    sister[initial[i]] = initial[i + 1];
  }

  std::vector<int> forward = std::move(sister);
  std::vector<int> backward(static_cast<std::size_t>(n + 1), 0);
  for (int i = 1; i <= n; ++i) {
    if (forward[i] < 0 || forward[i] > n) return std::nullopt;
    if (forward[i] != 0) backward[forward[i]] = i;
  }

  for (int i = 1; i <= n; ++i) {
    if (backward[i] != 0 || mother[i] == 0) continue;
    if (mother[i] < 1 || mother[i] > n) return std::nullopt;
    int tail = mother[i];
    int guard = 0;
    while (forward[tail] != 0) {
      tail = forward[tail];
      if (tail < 1 || tail > n || ++guard > 1000) return std::nullopt;
    }
    forward[tail] = i;
    backward[i] = tail;
  }

  int current = 0;
  for (int i = 1; i <= n; ++i) {
    if (backward[i] == 0) {
      current = i;
      break;
    }
  }
  if (current == 0) return std::nullopt;

  std::vector<int> order;
  order.reserve(static_cast<std::size_t>(n));
  std::vector<bool> seen(static_cast<std::size_t>(n + 1), false);
  for (int i = 0; i < n; ++i) {
    if (current < 1 || current > n || seen[current]) return std::nullopt;
    order.push_back(current);
    seen[current] = true;
    current = forward[current];
  }
  if (current != 0) return std::nullopt;
  return order;
}

DecodedTruth decodeCompactTruth(const CompactSimulationBanks& banks) {
  DecodedTruth result;
  const int lpvs = banks.pvs;
  const int lstsh = banks.stsh;
  const int npvs = ph::IQ(lpvs + 1);
  const int nsh0 = ph::IQ(lstsh + 2) / 1000;
  const int nstsh = ph::IQ(lstsh + 3) / 1000;
  const int nst0 = ph::IQ(lstsh + 4) / 1000;
  const int n = nsh0 + nstsh;
  if (npvs < 0 || nsh0 < 0 || nstsh < 0 || nst0 < 0 ||
      n <= 0 || n >= kMaxTruthParticles) {
    return result;
  }

  const int nwsh0 = ph::IQ(lstsh + 2) % 1000;
  const int nwstsh = ph::IQ(lstsh + 3) % 1000;
  const int lenhed = ph::IQ(lstsh + 1) - (nsh0 + 2 * nstsh + nst0) + 1;
  if (nwsh0 <= 0 || nwstsh <= 0 || lenhed <= 0) return result;

  std::vector<int> mother(static_cast<std::size_t>(n + 1), 0);
  std::vector<int> sister(static_cast<std::size_t>(n + 1), 0);
  int header = lstsh + lenhed;
  for (int i = 1; i <= nsh0; ++i) {
    const int packed = ph::IQ(header + i);
    mother[i] = bitField(packed, 9, 8);
    sister[i] = bitField(packed, 17, 8);
  }
  header += nsh0;
  for (int i = 1; i <= nstsh; ++i) {
    const int packed = ph::IQ(header + 2 * i - 1);
    mother[nsh0 + i] = bitField(packed, 9, 8);
    sister[nsh0 + i] = bitField(packed, 17, 8);
  }

  const auto maybeOrder = topologicalOrder(mother, std::move(sister));
  if (!maybeOrder) return result;
  const auto& order = *maybeOrder;
  std::unordered_map<int, int> shToLu;
  shToLu.reserve(order.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    shToLu[order[i]] = static_cast<int>(i + 1);
  }

  const int shData = lstsh + lenhed + nsh0 + 2 * nstsh + nst0;
  const int stshData = shData + nsh0 * nwsh0;
  result.particles.reserve(order.size());
  for (const int shIndex : order) {
    const bool unassociated = shIndex <= nsh0;
    const int record = unassociated
        ? shData + nwsh0 * (shIndex - 1)
        : stshData + nwstsh * (shIndex - nsh0 - 1);

    TruthParticle row;
    row.status = std::lround(ph::Q(record + (unassociated ? 8 : 13))) / 10000;
    row.pdg = std::lround(ph::Q(record + (unassociated ? 9 : 14)));
    const auto parent = shToLu.find(mother[shIndex]);
    row.parent = parent == shToLu.end() ? 0 : parent->second;
    row.momentum = {ph::Q(record + 3), ph::Q(record + 4),
                    ph::Q(record + 5)};
    row.mass = ph::Q(record + 2);

    if (!unassociated) {
      const int packedVertex =
          ph::IQ(lstsh + lenhed + nsh0 + 2 * (shIndex - nsh0));
      const int vertexIndex = bitField(packedVertex, 9, 8);
      if (vertexIndex > 0 && vertexIndex <= npvs) {
        const int vertex = lpvs + 1 + npvs + 6 * (vertexIndex - 1);
        row.vertex = {ph::Q(vertex + 4), ph::Q(vertex + 5),
                      ph::Q(vertex + 6)};
      }
    }
    result.particles.push_back(row);
  }

  // The first NSTSH compact ST records have an associated SH. Their downlinks
  // point back to reconstructed PA banks; this is PSHSIM's PA -> ST -> LU map.
  for (int st = 1; st <= nstsh; ++st) {
    const int lpa = ph::LQ(lstsh - st);
    const auto gen = shToLu.find(nsh0 + st);
    if (lpa > 0 && gen != shToLu.end()) result.lpaToGen[lpa] = gen->second - 1;
  }
  return result;
}

DecodedTruth decodeFullTruth() {
  DecodedTruth result;
  if (ph::LDTOP <= 0 || ph::IQ(ph::LDTOP - 2) <= 3) return result;

  std::unordered_map<int, int> originalToLu;
  int lsh = ph::LQ(ph::LDTOP - 3);
  while (lsh > 0 && result.particles.size() < kMaxTruthParticles) {
    if (std::lround(ph::Q(lsh + 1)) != 0) {
      const int lu = static_cast<int>(result.particles.size()) + 1;
      const int original = std::lround(ph::Q(lsh + 9));
      originalToLu[original] = lu;

      TruthParticle row;
      row.status = std::lround(ph::Q(lsh + 10)) / 10000;
      row.pdg = std::lround(ph::Q(lsh + 11));
      const int motherSh = ph::LQ(lsh - 2);
      if (motherSh > 0) {
        const auto parent = originalToLu.find(std::lround(ph::Q(motherSh + 9)));
        if (parent != originalToLu.end()) row.parent = parent->second;
      }
      row.momentum = {ph::Q(lsh + 3), ph::Q(lsh + 4), ph::Q(lsh + 5)};
      row.mass = ph::Q(lsh + 2);

      const int lst = ph::LQ(lsh - 4);
      if (lst > 0) {
        const int lpv = ph::LQ(lst + 1);
        if (lpv > 0) {
          row.vertex = {ph::Q(lpv + 5), ph::Q(lpv + 6), ph::Q(lpv + 7)};
        }
        const int lpa = ph::LQ(lst - 2);
        if (lpa > 0) result.lpaToGen[lpa] = lu - 1;
      }
      result.particles.push_back(row);
    }
    lsh = ph::LQ(lsh);
  }
  return result;
}

DecodedTruth decodeTruth() {
  if (const auto compact = compactSimulationBanks()) {
    return decodeCompactTruth(*compact);
  }
  return decodeFullTruth();
}

// Per-event MC truth interaction point (sim-PV), in mm. Used to shift the
// generator-frame vertices onto the DELSIM/reconstruction interaction point,
// preserving the existing converter contract.
std::optional<std::array<double, 3>> read_sim_pv_mm() {
  if (const auto compact = compactSimulationBanks()) {
    const int npvs = ph::IQ(compact->pvs + 1);
    if (npvs >= 1) {
      const int vertex = compact->pvs + 1 + npvs;
      return std::array<double, 3>{ph::Q(vertex + 4) * 10.0,
                                   ph::Q(vertex + 5) * 10.0,
                                   ph::Q(vertex + 6) * 10.0};
    }
  }
  if (ph::LDTOP <= 0 || ph::IQ(ph::LDTOP - 2) <= 3) return std::nullopt;
  for (int lsh = ph::LQ(ph::LDTOP - 3); lsh > 0; lsh = ph::LQ(lsh)) {
    if (std::lround(ph::Q(lsh + 1)) == 0) continue;
    const int lst = ph::LQ(lsh - 4);
    if (lst <= 0) continue;
    const int lpv = ph::LQ(lst + 1);
    if (lpv <= 0) continue;
    return std::array<double, 3>{ph::Q(lpv + 5) * 10.0,
                                 ph::Q(lpv + 6) * 10.0,
                                 ph::Q(lpv + 7) * 10.0};
  }
  return std::nullopt;
}

}  // namespace

// ---------------------------------------------------------------------------
void TruthGenWriter::emit() {
  GenParticleResult result;
  auto decoded = decodeTruth();
  const int nGen = static_cast<int>(decoded.particles.size());
  edm4hep::MCParticleCollection mc;
  result.handles.reserve(decoded.particles.size());

  double sx = 0.0, sy = 0.0, sz = 0.0;
  if (nGen >= 1) {
    if (auto spv = read_sim_pv_mm()) {
      sx = (*spv)[0] - static_cast<double>(decoded.particles[0].vertex[0]);
      sy = (*spv)[1] - static_cast<double>(decoded.particles[0].vertex[1]);
      sz = (*spv)[2] - static_cast<double>(decoded.particles[0].vertex[2]);
    }
  }

  for (const auto& row : decoded.particles) {
    auto mp = mc.create();
    mp.setPDG(row.pdg);
    mp.setGeneratorStatus(static_cast<std::int16_t>(row.status));
    mp.setMomentum({row.momentum[0], row.momentum[1], row.momentum[2]});
    mp.setMass(row.mass);
    mp.setVertex({static_cast<double>(row.vertex[0]) + sx,
                  static_cast<double>(row.vertex[1]) + sy,
                  static_cast<double>(row.vertex[2]) + sz});
    mp.setCharge(charge_from_pdg(row.pdg));
    result.handles.push_back(mp);
  }

  // Parent graph and the most-displaced-daughter endpoint convention are kept
  // identical to the previous PSCLUJ-backed writer.
  std::vector<double> bestDisp2(decoded.particles.size(), -1.0);
  for (int i = 0; i < nGen; ++i) {
    const int parentLu = decoded.particles[i].parent;
    if (parentLu < 1 || parentLu > nGen) continue;
    const std::size_t child = static_cast<std::size_t>(i);
    const std::size_t parent = static_cast<std::size_t>(parentLu - 1);
    result.handles[child].addToParents(result.handles[parent]);
    result.handles[parent].addToDaughters(result.handles[child]);
    const auto pv = result.handles[parent].getVertex();
    const auto cv = result.handles[child].getVertex();
    const double dx = cv.x - pv.x;
    const double dy = cv.y - pv.y;
    const double dz = cv.z - pv.z;
    const double displacement2 = dx * dx + dy * dy + dz * dz;
    if (displacement2 > bestDisp2[parent]) {
      result.handles[parent].setEndpoint(cv);
      bestDisp2[parent] = displacement2;
    }
  }

  result.lpa_to_gen = std::move(decoded.lpaToGen);
  put(std::move(mc), "LUJ", "GenParticles", Provenance::Derived);
  ctx_.gen_truth = std::move(result);
}

// ---------------------------------------------------------------------------
void TruthRecoLinkWriter::emit() {
  edm4hep::RecoMCParticleLinkCollection links;
  if (!ctx_.gen_truth || !ctx_.tracking) {
    put(std::move(links), "TBL", "RecoToGen", Provenance::Transcribed);
    return;
  }
  const auto& gen = *ctx_.gen_truth;
  const auto& tracking = *ctx_.tracking;

  // Iterate in VECP order to preserve the historical collection ordering.
  // The raw LPA address then resolves through the direct simulation-bank map.
  for (std::size_t j = 1; j < tracking.vecp_to_particle.size(); ++j) {
    const int particle = tracking.vecp_to_particle[j];
    if (particle < 0 ||
        particle >= static_cast<int>(tracking.particle_lpas.size())) continue;
    const auto found = gen.lpa_to_gen.find(tracking.particle_lpas[particle]);
    if (found == gen.lpa_to_gen.end() || found->second < 0 ||
        found->second >= static_cast<int>(gen.handles.size())) continue;

    auto link = links.create();
    link.setFrom(tracking.particle_handles[particle]);
    link.setTo(gen.handles[found->second]);
    link.setWeight(1.0f);
  }

  put(std::move(links), "TBL", "RecoToGen", Provenance::Transcribed);
}

}  // namespace delphi_edm4hep::truth
