#include "delphi_edm4hep/Geometry/CargoDatabase.h"

#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main() {
  std::istringstream input(R"(*MATC /AIR*.B
880101,0,940621,190801
*MATF  6,1,.129E-02,7.2,14.4,30050,0
**
*GEOM /DELF.B
890101,0,931220,214701
*DBF COMG (I1/20A4)
1
0+1
*MATS  2,AIR*,AIR*
*SHA1  7,CYL1,0,360,0,61,-800,800
*SHAP  7,CYL1,0,360,0,680,-585,585
**
)");

  const auto database =
      delphi_edm4hep::geometry::CargoDatabase::read(input, "fixture");
  require(database.records().size() == 2, "wrong record count");
  require(database.recordsOfKind("MATC").size() == 1, "wrong material count");
  require(database.recordsOfKind("GEOM").size() == 1, "wrong geometry count");

  const auto &world = database.records().at(1);
  require(world.path == "/DELF.B", "wrong world path");
  require(world.validity.validFromDate == 890101, "wrong validity date");
  require(world.validity.editedTime == 214701, "wrong edit time");
  require(world.findField("MATS") != nullptr, "missing material assignment");
  require(world.findField("SHAP") != nullptr, "missing final shape");
  const auto *comment = world.findField("DBF");
  require(comment != nullptr, "missing multi-line field");
  require(comment->continuation.size() == 2, "wrong continuation count");

  std::istringstream malformed(
      "*GEOM /BROKEN.B\n890101,0,931220,214701\n*SHAP 1,BOX,1\n");
  bool rejected = false;
  try {
    static_cast<void>(
        delphi_edm4hep::geometry::CargoDatabase::read(malformed, "broken"));
  } catch (const std::runtime_error &error) {
    require(std::string(error.what()).find("unterminated record") !=
                std::string::npos,
            "malformed input failed for the wrong reason");
    rejected = true;
  }
  require(rejected, "unterminated record was accepted");
}
