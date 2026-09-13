#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GdmlWorldWriter.h"
#include "delphi_edm4hep/Geometry/GeometryModel.h"

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
*MATS  2,AIR*,AIR*
*SHA1  7,CYL1,0,360,0,61,-800,800
*SHAP  7,CYL1,0,360,0,680,-585,585
**
)");
  const auto database =
      delphi_edm4hep::geometry::CargoDatabase::read(input, "fixture");
  const auto model =
      delphi_edm4hep::geometry::GeometryModel::fromCargo(database, "fixture");
  std::ostringstream output;
  delphi_edm4hep::geometry::writeGdmlWorld(output, model, "/DELF.B",
                                           "v94c<&\"");
  const auto gdml = output.str();
  require(gdml.find("rmax=\"680\"") != std::string::npos,
          "world radius was not written");
  require(gdml.find("z=\"1170\"") != std::string::npos,
          "world length was not written");
  require(gdml.find("<D unit=\"g/cm3\" value=\"") != std::string::npos,
          "world density was not written");
  require(gdml.find("v94c&lt;&amp;&quot;") != std::string::npos,
          "snapshot identifier was not XML escaped");
  require(gdml.find("SHA1") == std::string::npos,
          "auxiliary shape was mistaken for the world boundary");
}
