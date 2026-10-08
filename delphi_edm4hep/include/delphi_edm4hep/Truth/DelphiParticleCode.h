// DELPHI particle code -> PDG code.
//
// Particles created by the detector simulation (DELSIM) carry DELPHI's own
// particle code (the "mass code" of the simulated track), not a PDG code.
// The conversion follows the DELPHI convention routine UPCON (deck UPNACO,
// dstana10.car): PDG = sign(code) * VLUN(|code|), a negative code being the
// antiparticle. Only the codes DELSIM gives its own secondaries are covered:
// the GHEISHA table IKPART (delsim36.car) plus the leptons and photons of
// DELSIM's decays. Nuclei have no PDG code in VLUN and get nuclear PDG codes.

#pragma once

namespace delphi_edm4hep::truth {

// PDG code of a DELPHI particle code; 0 when the code is not covered,
// including GHEISHA's geantino (17).
int pdgFromDelphiCode(int delphiCode);

}  // namespace delphi_edm4hep::truth
