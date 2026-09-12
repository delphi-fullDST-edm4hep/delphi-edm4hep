// Dump SKELANA's own good-run selection, so runquality.py can be checked
// against the Fortran it reimplements.
//
// PSRUNQ reads every RUNQUALI file named in its FILNAM table and leaves the
// accepted runs in PSCRNQ as ranges, closed on the last accepted row before a
// rejection. This prints them.
//
// The PSCRNQ common block is declared here rather than in
// extern/delphi-analysis, which is vendored and would lose it on re-vendor.
// It belongs upstream with the other missing bindings.
//
// Build against a configured converter build, then run:
//
//   INC=$(grep -m1 '^CXX_INCLUDES' \
//         build/CMakeFiles/delphi_edm4hep.dir/flags.make | cut -d= -f2-)
//   g++ -std=c++20 -O2 $INC -Idelphi_edm4hep/extern/delphi-analysis/include \
//       -c scripts/runquality/psrunq_probe.cpp -o probe.o
//   # then link with build/CMakeFiles/delphi_sdst_pass.dir/link.txt,
//   # substituting probe.o for the pass's own object
//
// Usage: psrunq_probe [DET=MIN ...]
//   with no arguments, SKELANA's own window: VD >= 1 and TPC >= 7.

#include "skelana/functions.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// +KEEP,PSCRNQ (stdcdes.car).
constexpr int MRQFIL = 5000, NRQDET = 38, NRQTRG = 23, MSRUNS = 3000;

extern "C" struct {
  int   nrqfil;
  int   irqfil[MRQFIL];
  float erqfil[MRQFIL];
  int   irqdmn[NRQDET], irqdmx[NRQDET];
  int   irqtmn[NRQTRG], irqtmx[NRQTRG];
  int   irqdet[NRQDET], irqtrg[NRQTRG];
  int   nsruns;
  int   ifruns[MSRUNS], ilruns[MSRUNS];
  int   iffile[MSRUNS], ilfile[MSRUNS];
} pscrnq_;

// DETNAM from skelana.car, in order.
const char* const DETNAM[NRQDET] = {
  "MVX_A", "MVX_C", "ID_JET", "ID_TRIG", "TPC_0", "TPC_1", "BRICH_L",
  "BRICH_G", "OD_B", "OD_D", "HPC_0", "HPC_1", "HCAB_A", "HCAB_C", "MUB_B",
  "MUB_D", "FCA_A", "FCA_C", "RIF_A", "RIF_C", "FCB_A", "FCB_C", "EMF_A",
  "EMF_C", "HCAF_A", "HCAF_C", "MUF_A", "MUF_C", "SAT_CAL", "SAT_TRA",
  "VSAT", "VFT_PIX", "VFT_STR", "MUS", "TOF", "TAG_40", "TAG_90", "TAG_PHI",
};

}  // namespace

int main(int argc, char** argv) {
  // Wide open, as PSINI leaves them.
  for (int i = 0; i < NRQDET; ++i) { pscrnq_.irqdmn[i] = 0; pscrnq_.irqdmx[i] = 9; }
  for (int i = 0; i < NRQTRG; ++i) { pscrnq_.irqtmn[i] = 0; pscrnq_.irqtmx[i] = 9; }

  if (argc == 1) {                       // SKELANA's own window, from USER00
    pscrnq_.irqdmn[0] = pscrnq_.irqdmn[1] = 1;   // MVX_A, MVX_C
    pscrnq_.irqdmn[4] = pscrnq_.irqdmn[5] = 7;   // TPC_0, TPC_1
  }
  for (int a = 1; a < argc; ++a) {
    const char* eq = std::strchr(argv[a], '=');
    if (!eq) { std::fprintf(stderr, "expected DET=MIN, got %s\n", argv[a]); return 2; }
    int found = -1;
    for (int i = 0; i < NRQDET; ++i)
      if (std::strncmp(argv[a], DETNAM[i], eq - argv[a]) == 0
          && DETNAM[i][eq - argv[a]] == '\0') found = i;
    if (found < 0) { std::fprintf(stderr, "unknown detector %s\n", argv[a]); return 2; }
    pscrnq_.irqdmn[found] = std::atoi(eq + 1);
  }

  skelana::PSRUNQ(0);

  std::printf("# SKELANA PSRUNQ accepted ranges: firstRun firstFile lastRun lastFile\n");
  std::printf("# window:");
  for (int i = 0; i < NRQDET; ++i)
    if (pscrnq_.irqdmn[i] > 0 || pscrnq_.irqdmx[i] < 9)
      std::printf(" %s>=%d", DETNAM[i], pscrnq_.irqdmn[i]);
  std::printf("\n# fills %d, ranges %d\n", pscrnq_.nrqfil, pscrnq_.nsruns);
  if (pscrnq_.nsruns >= MSRUNS)
    std::fprintf(stderr, "warning: hit the MSRUNS cap, the list is truncated\n");
  for (int i = 0; i < pscrnq_.nsruns; ++i)
    std::printf("%d %d %d %d\n", pscrnq_.ifruns[i], pscrnq_.iffile[i],
                pscrnq_.ilruns[i], pscrnq_.ilfile[i]);
  return 0;
}
