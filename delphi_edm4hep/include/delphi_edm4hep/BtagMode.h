// BtagMode.h
//
// How the converter configures DELPHI's b-tagging for a job. Shared by the
// PHDST harness (which turns it into SKELANA IFLBTG / IFLPVT) and by
// BtagWriter (which decides what it can read back).
//
// The three modes are NOT symmetric between the two passes, because
// SKELANA's PSHORT dispatch isn't (skelana.car):
//
//     IF (FULLDST) THEN
//       IF ( IFLBTG .GT. 0 ) CALL PSFBTG          ! always recalculates
//     ELSE
//       IF ( IFLBTG .EQ. 1 ) CALL PSHBTG          ! reads the stored bank
//       IF ( IFLBTG .EQ. 2 ) CALL PSFBTG          ! recalculates
//     ENDIF
//
// so `Bank` degrades to a recalculation on the fullDST pass. BtagWriter
// resolves that per pass and names its output accordingly: bank-read
// values go under the BTG mnemonic, recalculated ones under AABTAG.

#pragma once

namespace delphi_edm4hep {

enum class BtagMode {
  // IFLBTG = 0. AABTAG is not run at all. BtagWriter still records its
  // source-local BTAGCFG steering/status provenance, but emits no BTG/AABTAG
  // payload. The default: the converter's job is to transcribe DST content,
  // and a b-tag rerun is a new reconstruction, not a transcription.
  Off,
  // IFLBTG = 1. ShortDST: read the legacy stored BTAG bank. This remains a
  // low-level compatibility mode for historical closure only; current
  // production uses Recalc. FullDST: SKELANA recalculates regardless — see
  // the dispatch above.
  Bank,
  // IFLBTG = 2. Recalculate with AABTAG on both passes. Recalculation (this
  // mode on either pass, or Bank on a fullDST) populates the per-track
  // AAMAIN / AAMNVX commons that the jet-probability inputs come from.
  Recalc,
};

// Whether AABTAG's own primary vertex should replace the DELANA primary
// vertex in SKELANA's PSCVTX common (IFLPVT).
//
// Default is Keep, and deliberately so. With Replace, PSFBTG overwrites
// QVTX(6..15,1) with AABTAG's vertex -- and on the beamspot-failure path
// (IERRBS != 0) it overwrites the position with the -999 sentinel,
// destroying a perfectly good DELANA vertex that PSHVTX had already
// filled. Since AABTAG's vertex is readable directly from AAMNVX
// (POSVX/COVVX) and BtagWriter emits it as its own collection, there is
// no reason to let it clobber the original.
enum class BtagPrimaryVertex {
  Keep,     // IFLPVT = 0 -- DELANA primary vertex
  Replace,  // IFLPVT = 1 -- AABTAG primary vertex
};

}  // namespace delphi_edm4hep
