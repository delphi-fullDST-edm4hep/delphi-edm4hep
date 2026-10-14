// Event domain: PHCIII + direct pilot/PA/VD queries + per-event B field.
// EventWriter emits per-event Frame parameters under "<prefix>_EVT_*".

#pragma once

#include "delphi_edm4hep/CollectionWriter.h"

namespace delphi_edm4hep::event {

class EventWriter : public CollectionWriter {
public:
  using CollectionWriter::CollectionWriter;
  void emit() override;
};

}  // namespace delphi_edm4hep::event
