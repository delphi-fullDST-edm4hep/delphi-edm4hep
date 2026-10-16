// BsaurusWords.h — internal header (not exported).
//
// Slot names of the three BSAURUS word arrays, transcribed from the PARAMETER
// statements of the CSAURUS patch in bsaurus.car (dstana release 161018):
// IBE_* for BSEVT, IBH_* for BSHEM, IBP_* for BSPAR. A multi-word entry
// (4-vector, position, packed covariance) is expanded into one slot per
// component. The names are written once per job into the metadata frame
// (`<prefix>_BSAURUS_*WordNames`) so a reader can look a slot up by name
// rather than by remembering the Fortran index; the tables below are the
// single source of truth for both the file and the README.
//
// Five names differ from the Fortran PARAMETERs, so that no emitted name
// suggests an EDM4hep jet: BSAURUS's LUCLUS objects are called clusters and
// its momentum-weighted charges hemisphere charges. The Fortran name of each
// is given in its comment.
//
// Units are BSAURUS's own: GeV, cm, ps for the lifetime. The typed
// collections (Hemispheres, SecondaryVertices) convert to mm; the word arrays
// do not.

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace delphi_edm4hep::bsaurus::words {

// One PARAMETER entry: `name` at 1-based Fortran slot `first`, `count` words.
struct Entry {
  std::string_view name;
  int              first;
  int              count;
};

// Suffixes used to expand a multi-word entry, by its count.
inline constexpr std::array<std::string_view, 4> kFourVector = {"px", "py", "pz", "E"};
inline constexpr std::array<std::string_view, 3> kPosition   = {"x", "y", "z"};
inline constexpr std::array<std::string_view, 6> kCovariance = {"xx", "xy", "yy", "xz", "yz", "zz"};

// ---- BSEVT (NVEVT = 20) -----------------------------------------------------
inline constexpr std::array<Entry, 11> kEvent = {{
  {"BTAG",     1, 1},   // event b-tag
  {"BCONF",    2, 1},   // event b-confidence (W. Murray)
  {"BCTAG",    3, 1},   // event b-tag with charm suppression
  {"BCOMB",    4, 1},   // AABTAG combined event tag
  {"PV",       5, 3},   // primary vertex (cm)
  {"PC",       8, 6},   // its packed covariance (cm^2)
  {"NCLUS",   14, 1},   // number of LUCLUS clusters (Fortran IBE_NJET)
  {"MC_FLAV", 15, 1},   // MC primary quark flavour (12 = b)
  {"MC_PV",   16, 3},   // MC primary vertex (cm)
  {"COST",    19, 1},   // cos(theta) of the thrust axis
  {"RICH",    20, 1},   // barrel RICH status: 3 gas+liquid, 2 liquid, 1 gas, 0 none
}};

// ---- BSHEM (NVHEM = 165) ----------------------------------------------------
inline constexpr std::array<Entry, 134> kHemisphere = {{
  {"BTAG",        1, 1}, {"BCONF",       2, 1}, {"BCTAG",       3, 1},
  {"BF",          4, 4},   // B 4-vector from the vertex fit
  {"BT",          8, 4},   // fit + energy correction
  {"BY",         12, 4},   // raw, from the rapidity algorithm
  {"SV",         16, 3},   // secondary vertex (cm)
  {"SC",         19, 6},   // its packed covariance (cm^2)
  {"DEC",        25, 1},   // r-phi decay length (cm)
  {"DECS",       26, 1},   // its error
  {"PROB",       27, 1},   // secondary-vertex fit probability
  {"RET",        28, 1},   // ISAURUS fit return code
  {"NACC",       29, 1},   // particles at the secondary vertex
  {"QHEM",       30, 1},   // momentum-weighted hemisphere charge, kappa = 0.6 (IBH_QJET)
  {"QVER",       31, 1},   // vertex-charge net
  {"QVERS",      32, 1},   // its error
  {"FLAV",       33, 1},   // b flavour net, 0..1: > 0.5 on the b-quark side (official-MC truth), -2 no result
  {"MY",         34, 1},   // rapidity mass
  {"SHEM",       35, 1},   // scaled measured hemisphere energy
  {"QUAL",       36, 1},   // hemisphere quality flag
  {"MC_BDEC",    37, 1}, {"MC_DDEC",    38, 1}, {"MC_DPDEC",   39, 1},
  {"MC_BV",      40, 3}, {"MC_DV",      43, 3}, {"MC_DPV",     46, 3},
  {"MC_DST",     49, 1},
  {"MVER",       50, 1},   // mass at the secondary vertex
  {"BPLUS",      51, 1},   // B+ tagging net
  {"PRPF",       52, 1}, {"PRPB",       53, 1}, {"PRSF",       54, 1}, {"PRSB",       55, 1},
  {"DIPOL",      56, 1},   // secondary-vertex dipole moment
  {"HKST",       57, 1},
  {"MC_BP",      58, 4},   // MC B 4-vector
  {"MC_BKF",     62, 1}, {"MC_ND",      63, 1}, {"MC_OSCB",    64, 1},
  {"NPCUT",      65, 1}, {"NPALL",      66, 1}, {"NPGOOD",     67, 1},
  {"DEC3D",      68, 1},   // 3-D decay length (cm)
  {"DEC3DS",     69, 1},
  {"BLIFE",      70, 1},   // B lifetime (ps)
  {"BLIFES",     71, 1},
  {"MC_BLIFE",   72, 1},
  {"PKAON",      73, 1}, {"PLEP",       74, 1},
  {"CHRDIP",     75, 1},   // B-D charge dipole moment
  {"MC_QTYP",    76, 1}, {"MC_PBKF",    77, 1},
  {"SVVAR1",     78, 1}, {"SVVAR2",     79, 1},
  {"BCOMB",      80, 1},   // AABTAG combined tag of the hemisphere
  {"DIPPY",      81, 1},
  {"RAWQ",       82, 1},   // vertex charge, sum of track charges
  {"QRANK1",     83, 1},
  {"DELTA",      84, 1},
  {"HNUM",       85, 1},   // hemisphere number
  {"SHEM2",      86, 1},
  {"QHEM2",      87, 1},   // the same, kappa = 0.3 (IBH_QJET2)
  {"QLEAD",      88, 1},   // charge of the particle with the largest momentum along the axis (IBH_QJET3)
  {"MC_DRSKF",   89, 1}, {"MC_QNDRS",   90, 1}, {"MC_QCDRS",   91, 1}, {"MC_DRSP",    92, 1},
  {"MC_BMC",     93, 1}, {"MC_BMTOT",   94, 1},
  {"QRANK2",     95, 1},
  {"NKDECAY",    96, 1}, {"QKAMAX",     97, 1}, {"QFSUM",      98, 1}, {"NKFRAG",     99, 1},
  {"NPDECAY",   100, 1}, {"NPFRAG",    101, 1},
  {"SIGPHI",    102, 1},   // direction errors from the SV fit (rad)
  {"SIGTHET",   103, 1},
  {"NKS",       104, 1}, {"NLAM0",     105, 1}, {"NNEUT",     106, 1},
  {"MC_SLD",    107, 1},
  {"QRANK3",    108, 1},
  {"YRANK1",    109, 1}, {"YRANK2",    110, 1}, {"YRANK3",    111, 1},
  {"KRANK1",    112, 1}, {"KRANK2",    113, 1}, {"KRANK3",    114, 1},
  {"PRANK1",    115, 1}, {"PRANK2",    116, 1}, {"PRANK3",    117, 1},
  {"NPION",     118, 1}, {"NWPROTD",   119, 1}, {"NPGC",      120, 1}, {"NWPROTF",   121, 1},
  {"NWLAMBD",   122, 1}, {"NWLAMBF",   123, 1}, {"NWKSD",     124, 1}, {"NWKSF",     125, 1},
  {"NWKD",      126, 1}, {"NWKF",      127, 1},
  {"PICHRG",    128, 1},
  {"QKAON",     129, 1}, {"QLEP",      130, 1},
  {"BZERO",     131, 1},   // B0 tagging net
  {"MC_BDEC2D", 132, 1},
  {"DFLBS",     133, 1}, {"DFLB0",     134, 1}, {"DFLBP",     135, 1}, {"DFLLB",     136, 1},
  {"FFLBS",     137, 1}, {"FFLB0",     138, 1}, {"FFLBP",     139, 1}, {"FFLLB",     140, 1},
  {"PRBBS",     141, 1}, {"PRBB0",     142, 1}, {"PRBBP",     143, 1}, {"PRBLB",     144, 1},
  {"FLAV45",    145, 1},   // improved b flavour net, 0..1: > 0.5 on the b-quark side, -2 no result
  {"PRBAIBS",   146, 1}, {"PRBAIBP",   147, 1}, {"PRBAIB0",   148, 1}, {"PRBAILB",   149, 1},
  {"DFLAV45",   150, 1},
  {"LIKBS",     151, 1}, {"LIKB0",     152, 1},
  {"QUAL2",     153, 1},
  {"BTT",       154, 4},   // 'optimal' B 4-vector
  {"YBFIT",     158, 1},
  {"BCLUS",     159, 4},   // 4-vector of the cluster chosen as the B candidate (IBH_BJET)
  {"NCLUS",     163, 1},   // LUCLUS clusters in the hemisphere (IBH_NJET)
  {"WEIGHT",    164, 1},   // SIMWAIT simulation weight
  {"FLBOTH",    165, 1},   // likelihood combination with the opposite side
}};

// ---- BSPAR (NVPAR = 47) -----------------------------------------------------
inline constexpr std::array<Entry, 47> kTrack = {{
  {"HEM",        1, 1},   // hemisphere number
  {"Y",          2, 1},   // rapidity w.r.t. the B-candidate axis
  {"PRP",        3, 1}, {"PRS",        4, 1},
  {"BRP",        5, 1}, {"BZ",         6, 1}, {"B3D",        7, 1},
  {"BSRP",       8, 1}, {"BSZ",        9, 1}, {"BS3D",      10, 1},
  {"NET",       11, 1},   // track net: 0 fragmentation, 1 B decay
  {"NRP",       12, 1}, {"NZ",        13, 1},
  {"KST",       14, 1},
  {"MC_KF",     15, 1}, {"MC_KFP",    16, 1}, {"MC_TYP",    17, 1},
  {"MC_KST",    18, 1}, {"MC_THETST", 19, 1},
  {"THETST",    20, 1},
  {"BSAU",      21, 1},   // BSAURUS internal particle index
  {"SV",        22, 1},   // 1 if in the secondary-vertex fit
  {"TRKE",      23, 1}, {"TRKM",      24, 1}, {"TRKP",      25, 1}, {"TRKQ",      26, 1},
  {"TRKL",      27, 1}, {"ERRE",      28, 1}, {"DELPP",     29, 1},
  {"MC_TRKP",   30, 1}, {"MC_TRKE",   31, 1},
  {"QUAL",      32, 1},
  {"DNET",      33, 1},   // cascade-D track net
  {"DFLBS",     34, 1}, {"DFLB0",     35, 1}, {"DFLBP",     36, 1}, {"DFLLB",     37, 1},
  {"FFLBS",     38, 1}, {"FFLB0",     39, 1}, {"FFLBP",     40, 1}, {"FFLLB",     41, 1},
  {"IYB",       42, 1},   // matching SKELANA VECP index (charged only)
  {"WFRY",      43, 1}, {"WLFY",      44, 1}, {"WBDY",      45, 1},
  {"MACK",      46, 1},   // MACRIB kaon net
  {"MACP",      47, 1},   // MACRIB proton net
}};

// Fortran slot indices the writer needs by name.
inline constexpr int IBH_SV      = 16;
inline constexpr int IBH_SC      = 19;
inline constexpr int IBH_DEC     = 25;
inline constexpr int IBH_DECS    = 26;
inline constexpr int IBH_PROB    = 27;
inline constexpr int IBH_NACC    = 29;
inline constexpr int IBH_DEC3D   = 68;
inline constexpr int IBH_DEC3DS  = 69;
inline constexpr int IBH_RAWQ    = 82;
inline constexpr int IBH_SIGPHI  = 102;
inline constexpr int IBH_SIGTHET = 103;
inline constexpr int IBH_FLAV45  = 145;
inline constexpr int IBH_BTT     = 154;
inline constexpr int IBP_HEM     = 1;
inline constexpr int IBP_NET     = 11;
inline constexpr int IBP_SV      = 22;

// Expand a table into one name per slot, in slot order. `total` must equal
// the number of words in the Fortran array; the test checks the tables cover
// 1..total exactly once.
template <std::size_t N>
std::vector<std::string> expand(const std::array<Entry, N>& table, int total) {
  std::vector<std::string> names(static_cast<std::size_t>(total));
  for (const Entry& e : table) {
    for (int c = 0; c < e.count; ++c) {
      std::string n(e.name);
      if (e.count == 4)      n += std::string("_") + std::string(kFourVector[c]);
      else if (e.count == 3) n += std::string("_") + std::string(kPosition[c]);
      else if (e.count == 6) n += std::string("_") + std::string(kCovariance[c]);
      const int slot = e.first + c;
      if (slot >= 1 && slot <= total) names[static_cast<std::size_t>(slot - 1)] = std::move(n);
    }
  }
  return names;
}

}  // namespace delphi_edm4hep::bsaurus::words
