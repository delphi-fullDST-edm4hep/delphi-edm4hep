// Truth domain — TruthGenWriter + TruthRecoLinkWriter.
//
// TruthGenWriter emits the simulation record of the STSH bank: the generator
// event (PSCLUJ, filled by PSHLUJ on sDST or PSFLUJ on fDST per bank-presence
// gating) and the particles the detector simulation added (the simulated
// tracks of PSCVEC / PSCTBL without a generator line). TruthRecoLinkWriter
// links reconstructed particles to them through the exact PSCTBL tables.

#include "delphi_edm4hep/Truth/Truth.h"
#include "delphi_edm4hep/Truth/DelphiParticleCode.h"

#include "phdst/uxcom.hpp"
#include "phdst/uxlink.hpp"
#include "skelana/mtrack.hpp"
#include "skelana/pscluj.hpp"
#include "skelana/psctbl.hpp"
#include "skelana/pscvec.hpp"
#include "skelana/pscvtx.hpp"

#include <edm4hep/MCParticleCollection.h>
#include <edm4hep/RecoMCParticleLinkCollection.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <set>
#include <vector>

// JETSET helpers. Charges via LUCHGE in units of e/3 (exhaustive PDG
// coverage; replaces the 16-entry whitelist of the legacy code).
extern "C" {
  void pshluj_();
  void psfluj_();
  int  luchge_(int* kf);
}

namespace ph = phdst;
namespace sk = skelana;

namespace delphi_edm4hep::truth {

namespace {

inline float charge_from_pdg(int pdg) {
  return static_cast<float>(luchge_(&pdg)) / 3.0f;
}

// Gate the PSCLUJ unpacker on bank presence in the LDTOP chain, invoke
// the sDST or fDST routine, return sk::NP. (full-DST SH chain at
// LDTOP-3, sDST condensed LU at LDTOP-28/-29.)
int unpack_lujets() {
  sk::NP = 0;
  if (ph::LDTOP <= 0) return 0;
  const bool short_sim = (ph::IQ(ph::LDTOP - 2) > 28)
                      && (ph::LQ(ph::LDTOP - 28) != 0)
                      && (ph::LQ(ph::LDTOP - 29) != 0);
  const bool full_sim  = (ph::LQ(ph::LDTOP - 3) != 0);
  if      (short_sim) pshluj_();
  else if (full_sim)  psfluj_();
  return sk::NP;
}

constexpr double kCm2Mm = 10.0;

// Per-event MC truth interaction point (sim-PV), in mm. Handles BOTH sim
// layouts: shortDST (LPVS at LDTOP-28, first sim-PV x/y/z at Q(ip+4..6) cm) and
// fullDST (LDTOP-3 -> LSH -> LST -> LPV, x/y/z at Q(lpv+5..7) cm); same decode
// as delphi-raw-nanoaod fillSimPV. Returns nullopt when no sim banks are present
// (real data / missing bank). Used as the production vertex of generator-only
// entries, which DELSIM gives no vertex of their own.
std::optional<std::array<double, 3>> read_sim_pv_mm() {
  if (ph::LDTOP <= 0) return std::nullopt;
  // shortDST sim layout: LPVS at LDTOP-28, x/y/z at +4/+5/+6 (cm). Guard the
  // down-link index exactly as unpack_lujets() does -- LDTOP-28 is only a valid
  // link when the LDTOP bank has >28 down-links -- so this is safe on real data
  // (and on fullDST, where it falls through to the LSH-chain walk below).
  if (ph::IQ(ph::LDTOP - 2) > 28) {
    const int lpvs = ph::LQ(ph::LDTOP - 28);
    if (lpvs > 0) {
      const int npvs = ph::IQ(lpvs + 1);
      if (npvs >= 1) {
        const int ip = lpvs + 1 + npvs;
        return std::array<double, 3>{ph::Q(ip + 4) * kCm2Mm,
                                     ph::Q(ip + 5) * kCm2Mm,
                                     ph::Q(ip + 6) * kCm2Mm};
      }
    }
  }
  // fullDST sim layout (our DELSIM SDST takes this path): walk the LSH chain at
  // LDTOP-3, first SH with Q(lsh+1)!=0 -> LST at LSH-4 -> LPV at LST+1; x/y/z at
  // LPV+5/+6/+7 (cm). Same decode as raw-nanoaod fillSimPV (PSFLUJ, skelana.car ~L7088).
  for (int lsh = ph::LQ(ph::LDTOP - 3); lsh > 0; lsh = ph::LQ(lsh)) {
    if (std::lround(ph::Q(lsh + 1)) == 0) continue;
    const int lst = ph::LQ(lsh - 4);
    if (lst <= 0) continue;
    const int lpv = ph::LQ(lst + 1);
    if (lpv <= 0) continue;
    return std::array<double, 3>{ph::Q(lpv + 5) * kCm2Mm,
                                 ph::Q(lpv + 6) * kCm2Mm,
                                 ph::Q(lpv + 7) * kCm2Mm};
  }
  return std::nullopt;
}

// The simulated tracks (ST) SKELANA unpacks from STSH: track number 1..NST.
// Their momenta sit in the second half of VECP, their vertices in the
// simulated half of QVTX, and PSCTBL relates them to the generator lines.
struct SimulatedTrack {
  int number;
  float vec(int word) const { return sk::VECP(word, sk::MTRACK + number); }
  int generatorLine() const { return sk::ISTLU(number); }      // 0: made by DELSIM
  int originVertex() const { return sk::ISTVX(1, number); }
  int endVertex() const { return sk::ISTVX(2, number); }       // 0: none
};

// The second half of VECP holds MTRACK simulated tracks.
int simulatedTrackCount() {
  if (sk::NST() > sk::MTRACK) {
    static bool warned = false;
    if (!warned) {
      std::cerr << "delphi_edm4hep::truth: " << sk::NST()
                << " simulated tracks, only the first " << sk::MTRACK
                << " fit SKELANA's track vector\n";
      warned = true;
    }
    return sk::MTRACK;
  }
  return sk::NST();
}

// Position (mm) of simulated vertex k; empty when k is not a vertex.
std::optional<edm4hep::Vector3d> simulatedVertexPosition(int k) {
  if (k < 1 || k > sk::NVTXMX) return std::nullopt;
  const int column = sk::NVTXMX + k;
  return edm4hep::Vector3d{sk::QVTX(6, column) * kCm2Mm,
                           sk::QVTX(7, column) * kCm2Mm,
                           sk::QVTX(8, column) * kCm2Mm};
}

// The simulated track entering simulated vertex k; 0 when none.
int trackEnteringVertex(int k) {
  if (k < 1 || k > sk::NVTXMX) return 0;
  return sk::KVTX(2, sk::NVTXMX + k);
}

// PDG code of a simulated track's DELPHI particle code. A code outside the
// table gives 0 and is reported once.
int pdgOfSimulatedTrack(const SimulatedTrack& track) {
  const int delphiCode = static_cast<int>(std::lround(track.vec(8)));
  const int pdg = pdgFromDelphiCode(delphiCode);
  static std::set<int> reported;
  if (pdg == 0 && reported.insert(delphiCode).second) {
    std::cerr << "delphi_edm4hep::truth: DELPHI particle code " << delphiCode
              << " has no PDG code; written as 0\n";
  }
  return pdg;
}

}  // namespace

// ---------------------------------------------------------------------------
void TruthGenWriter::emit() {
  GenParticleResult result;

  const int nGen = unpack_lujets();
  edm4hep::MCParticleCollection mc;
  result.handles.reserve(static_cast<std::size_t>(nGen));

  // Gated on nGen>=1 so real data (no LUJETS) never reads sim banks.
  const auto spv = nGen >= 1 ? read_sim_pv_mm() : std::nullopt;

  // DELSIM records a production vertex (cm, in the frame of the
  // reconstruction; PSFLUJ, skelana.car:7125-7127; PSHLUJ, skelana.car:
  // 5390-5392) only for the particles it tracked through the detector.
  // Generator-only entries -- partons, resonances and the products of decays
  // done by the generator -- have VP = 0; they are placed from the decay chain
  // below. LUJETS lists every parent before its daughters.
  using Position = std::optional<std::array<double, 3>>;
  std::vector<Position> productionPoint(nGen + 1), decayPoint(nGen + 1);
  for (int i = 1; i <= nGen; ++i) {
    const bool trackedByDelsim =
        sk::VP(i, 1) != 0.f || sk::VP(i, 2) != 0.f || sk::VP(i, 3) != 0.f;
    if (trackedByDelsim)
      productionPoint[i] = {sk::VP(i, 1) * kCm2Mm, sk::VP(i, 2) * kCm2Mm,
                            sk::VP(i, 3) * kCm2Mm};
  }

  // Decay point of each particle: where one of its tracked daughters starts.
  // A generator-only daughter is a resonance that decays where it is made, so
  // its own decay point stands in when no daughter was tracked. Walking the
  // record backwards visits every daughter before its parent.
  for (int daughter = nGen; daughter >= 1; --daughter) {
    const int parent = sk::KP(daughter, 3);
    if (parent < 1 || parent >= daughter) continue;
    if (productionPoint[daughter])  decayPoint[parent] = productionPoint[daughter];
    else if (!decayPoint[parent])   decayPoint[parent] = decayPoint[daughter];
  }

  // A generator-only particle starts where its parent decayed; failing that,
  // where its parent started; with no parent, at the simulated primary vertex.
  for (int particle = 1; particle <= nGen; ++particle) {
    if (productionPoint[particle]) continue;
    const int parent = sk::KP(particle, 3);
    if (parent < 1 || parent >= particle)  productionPoint[particle] = spv;
    else if (decayPoint[parent])           productionPoint[particle] = decayPoint[parent];
    else                                   productionPoint[particle] = productionPoint[parent];
  }

  // First pass: per-LU-index handle creation. KP(i,1)=status, KP(i,2)=PDG,
  // PP(i,1..3)=p, PP(i,5)=mass, VP(i,1..3)=production vertex (cm).
  for (int i = 1; i <= nGen; ++i) {
    auto mp = mc.create();
    mp.setPDG(sk::KP(i, 2));
    mp.setGeneratorStatus(static_cast<std::int16_t>(sk::KP(i, 1)));
    mp.setMomentum({sk::PP(i, 1), sk::PP(i, 2), sk::PP(i, 3)});
    mp.setMass(sk::PP(i, 5));
    if (const auto& v = productionPoint[i]) mp.setVertex({(*v)[0], (*v)[1], (*v)[2]});
    mp.setCharge(charge_from_pdg(sk::KP(i, 2)));
    result.handles.push_back(mp);
  }

  // Second pass: parent/daughter graph (parent edges only; daughters
  // are recoverable by reverse traversal).
  // Endpoint (decay vertex): a decay's real daughters sit at the (possibly
  // displaced) decay vertex, but LUJETS documentation-copy daughters (status 21)
  // sit at the PARENT's production vertex. Pick the MOST-displaced daughter so a
  // coincident doc-copy can't clobber a real displaced vertex -- the previous
  // "last-daughter-wins" silently dropped ~40% of displaced B/D endpoints. A
  // particle whose only daughters are coincident gets a zero-flight endpoint at
  // its own vertex (correct); stable particles (no daughters) keep the default.
  std::vector<double> best_disp2(static_cast<std::size_t>(nGen), -1.0);
  for (int i = 1; i <= nGen; ++i) {
    const int parent_lu = sk::KP(i, 3);
    if (parent_lu >= 1 && parent_lu <= nGen) {
      const std::size_t child = static_cast<std::size_t>(i - 1);
      const std::size_t pidx  = static_cast<std::size_t>(parent_lu - 1);
      result.handles[child].addToParents(result.handles[pidx]);
      result.handles[pidx].addToDaughters(result.handles[child]);
      const auto pv = result.handles[pidx].getVertex();
      const auto cv = result.handles[child].getVertex();
      const double dx = cv.x - pv.x, dy = cv.y - pv.y, dz = cv.z - pv.z;
      const double d2 = dx * dx + dy * dy + dz * dz;
      if (d2 > best_disp2[pidx]) {
        result.handles[pidx].setEndpoint(cv);
        best_disp2[pidx] = d2;
      }
    }
  }

  // MCParticle of each simulated track, by track number (entry 0 unused).
  // A track with a generator line is that line's particle; a track made by
  // DELSIM (decay products, interactions in the detector) gets a new one.
  std::vector<edm4hep::MutableMCParticle> particleOfTrack(simulatedTrackCount() + 1);
  for (int number = 1; number <= simulatedTrackCount(); ++number) {
    const SimulatedTrack track{number};
    const int line = track.generatorLine();
    if (line >= 1 && line <= nGen) {
      particleOfTrack[number] = result.handles[line - 1];
      continue;
    }
    auto particle = mc.create();
    particle.setPDG(pdgOfSimulatedTrack(track));
    particle.setGeneratorStatus(0);
    particle.setCreatedInSimulation(true);
    particle.setMomentum({track.vec(1), track.vec(2), track.vec(3)});
    particle.setMass(track.vec(5));
    particle.setCharge(track.vec(7));
    const auto origin = simulatedVertexPosition(track.originVertex());
    if (origin) {
      particle.setVertex(*origin);
    }
    particleOfTrack[number] = particle;
  }

  // Where DELSIM ended a track (decay or interaction), which replaces the
  // estimate from generator daughters above and is the only one for the
  // particles DELSIM decays (K0S, Lambda, ...). Who made each DELSIM
  // particle: the track entering its origin vertex.
  for (int number = 1; number <= simulatedTrackCount(); ++number) {
    const SimulatedTrack track{number};
    auto& particle = particleOfTrack[number];
    const auto end = simulatedVertexPosition(track.endVertex());
    if (end) {
      particle.setEndpoint(*end);
    }
    if (track.generatorLine() >= 1) continue;
    const int parentTrack = trackEnteringVertex(track.originVertex());
    if (parentTrack < 1 || parentTrack > simulatedTrackCount()) continue;
    particle.addToParents(particleOfTrack[parentTrack]);
    particleOfTrack[parentTrack].addToDaughters(particle);
  }
  result.particleOfSimulatedTrack = std::move(particleOfTrack);

  put(std::move(mc), "STSH", "MCParticles", Provenance::Derived);
  ctx_.gen_truth = std::move(result);
}

// ---------------------------------------------------------------------------
void TruthRecoLinkWriter::emit() {
  edm4hep::RecoMCParticleLinkCollection links;

  // Need both upstream writers' outputs.
  if (!ctx_.gen_truth || !ctx_.tracking) {
    put(std::move(links), "TBL", "RecoToMC", Provenance::Transcribed);   // emit empty + return
    return;
  }
  const auto& gen      = *ctx_.gen_truth;
  const auto& tracking = *ctx_.tracking;

  // PSCTBL.NPA = # PA particles = VECP entries. For each VECP index j:
  //   simulatedTrack = IPAST(j)            (0 if no simulated track)
  //   particle_idx = vecp_to_particle[j]   (-1 if Tracking dropped it)
  // The simulated track's MCParticle is its generator particle, or the one
  // DELSIM created (e.g. a Lambda decay product).
  const int nPA   = sk::NPA();
  const auto& v2p = tracking.vecp_to_particle;
  const auto& particleOfTrack = gen.particleOfSimulatedTrack;

  for (int j = 1; j <= nPA; ++j) {
    if (j >= static_cast<int>(v2p.size())) break;
    const int particle_idx = v2p[j];
    if (particle_idx < 0) continue;
    const int simulatedTrack = sk::IPAST(j);
    if (simulatedTrack < 1) continue;
    if (simulatedTrack >= static_cast<int>(particleOfTrack.size())) continue;

    auto link = links.create();
    link.setFrom(tracking.particle_handles[particle_idx]);
    link.setTo  (particleOfTrack[simulatedTrack]);
    link.setWeight(1.0f);
  }

  put(std::move(links), "TBL", "RecoToMC", Provenance::Transcribed);
}

}  // namespace delphi_edm4hep::truth
