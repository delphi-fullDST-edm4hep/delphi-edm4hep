// Validation-only SKELANA oracle. Production binaries do not compile or link
// this adapter; optional reference targets use it for migration comparisons.

#pragma once

namespace delphi_edm4hep::legacy_skelana {

void initialize();
void processRecord();

}  // namespace delphi_edm4hep::legacy_skelana
