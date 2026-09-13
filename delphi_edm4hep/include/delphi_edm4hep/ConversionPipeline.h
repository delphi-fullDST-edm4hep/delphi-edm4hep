#pragma once

namespace delphi_edm4hep::harness {
  struct Config;
}

namespace delphi_edm4hep::pipeline {

// Install the production, SKELANA-free preparation and writer graph for one
// conversion pass. Standalone executables and the native Code4hep source call
// these same functions so their physics output cannot drift independently.
void configureSdst(harness::Config&);
void configureFdst(harness::Config&);

}  // namespace delphi_edm4hep::pipeline
