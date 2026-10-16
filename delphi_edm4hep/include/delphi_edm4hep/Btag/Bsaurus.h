// Bsaurus.h — BSAURUS inclusive B-reconstruction writer.
//
// Runs DELPHI's BSAURUS package (DELPHI 2000-069 PHYS 868) on the current
// event and emits its output under <prefix>_BSAURUS_*:
//
//   Hemispheres        two ReconstructedParticles, one per thrust hemisphere,
//                      carrying the 'optimal' B 4-vector, the B candidate's
//                      secondary vertex as decayVertex, and the tracks fitted
//                      to that vertex as particles
//   SecondaryVertices  the fitted B decay vertex of each hemisphere that has one
//   HemisphereWords    one ParticleID per hemisphere with all 165 BSHEM words
//   TrackWords         one ParticleID per BSAURUS particle with all 47 BSPAR
//                      words, linked to its particle
//   frame parameters   Status (ISAURUS per hemisphere), EventWords (BSEVT),
//                      HemisphereClusterIndex, NParticles, NCharged
//
// The slot names of the three word arrays go once into the metadata frame.
//
// BSAURUS re-runs AABTAG, reconfigures JETSET and MAMMOTH and draws random
// numbers; the writer runs it inside its own copy of the Fortran commons it
// touches (restored or context-switched, see Bsaurus.cpp) so every event that
// follows is bit-identical to a job without BSAURUS. It also rewrites mass
// codes in the PA banks (PXLEID), so it must be the LAST writer of the event.

#pragma once

#include "delphi_edm4hep/CollectionWriter.h"

namespace delphi_edm4hep::bsaurus {

class BsaurusWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;

  void emit() override;
};

}  // namespace delphi_edm4hep::bsaurus
