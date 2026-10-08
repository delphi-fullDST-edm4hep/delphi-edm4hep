#include "delphi_edm4hep/Truth/DelphiParticleCode.h"

#include <cstdlib>

namespace delphi_edm4hep::truth {

int pdgFromDelphiCode(int delphiCode) {
  // A negative DELPHI code is the antiparticle of the positive one, except
  // -39, which DELPHI uses for the triton.
  if (delphiCode == -39) return 1000010030;   // triton
  const int sign = delphiCode < 0 ? -1 : +1;
  switch (std::abs(delphiCode)) {
    case  1: return sign * 12;          // electron neutrino
    case  2: return sign * 11;          // electron
    case  5: return sign * 14;          // muon neutrino
    case  6: return sign * 13;          // muon
    case  9: return sign * 16;          // tau neutrino
    case 21: return 22;                 // photon
    case 39: return 1000010020;         // deuteron
    case 40: return 1000020040;         // alpha
    case 41: return sign * 211;         // pi+
    case 42: return sign * 321;         // K+
    case 43: return sign * 311;         // K0
    case 47: return 111;                // pi0
    case 61: return 310;                // K0S
    case 62: return 130;                // K0L
    case 65: return sign * 2212;        // proton
    case 66: return sign * 2112;        // neutron
    case 67: return sign * 3222;        // Sigma+
    case 68: return sign * 3212;        // Sigma0
    case 69: return sign * 3112;        // Sigma-
    case 70: return sign * 3322;        // Xi0
    case 71: return sign * 3312;        // Xi-
    case 81: return sign * 3122;        // Lambda
    case 94: return sign * 3334;        // Omega-
    default: return 0;                  // includes 17, GHEISHA's geantino
  }
}

}  // namespace delphi_edm4hep::truth
