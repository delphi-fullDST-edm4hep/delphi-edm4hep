#pragma once

#include <array>
#include <vector>

namespace delphi_edm4hep::stic {

struct Row {
  int vecpIndex = -1;
  int lpa = 0;
  std::array<float, 3> measurement{};
  std::array<int, 6> attributes{};
};

// Decode full-DST PA.STIC modules without the SKELANA PSCSTC common.
void refreshFromFullDst();

// Decode PA.STIC/PA.SSTC modules from the current short-DST record.
void refreshFromSdst();

// Compatibility seam used only by the optional SKELANA validation oracle.
void setLegacyRows(std::vector<Row> rows);

const std::vector<Row>& current();

}  // namespace delphi_edm4hep::stic
