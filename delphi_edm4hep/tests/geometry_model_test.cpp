#include "delphi_edm4hep/Geometry/CargoDatabase.h"
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
*MATF  6,1,.129D-02,7.2,14.4,30050,0
**
*GEOM /DELF.B
890101,0,931220,214701
*MATS  2,AIR*,AIR*
*REFR  6,0,5.52,0,0,90,0
*SHA1  4,BRIK,.14,.16,56.6
*SHAP  7,CYL1,0,360,0,680,-585,585
**
)");

  const auto database =
      delphi_edm4hep::geometry::CargoDatabase::read(input, "fixture");
  const auto model =
      delphi_edm4hep::geometry::GeometryModel::fromCargo(database, "fixture");
  require(model.materials().size() == 1, "wrong material count");
  require(model.materials()[0].name == "AIR*", "wrong material name");
  require(model.materials()[0].radiationLengthProvided,
          "wrong radiation-length flag");
  require(model.materials()[0].densityGramPerCm3 == .00129,
          "Fortran D exponent was not parsed");
  require(model.nodes().size() == 1, "wrong node count");
  const auto &world = model.nodes()[0];
  require(world.name == "DELF", "wrong node name");
  require(world.materials.size() == 1, "wrong material assignment count");
  require(world.shapes.size() == 2, "wrong shape count");
  require(world.shapes[0].kind ==
              delphi_edm4hep::geometry::DelphiShapeKind::Brick,
          "wrong brick classification");
  require(world.shapes[1].parameters[4] == -585, "wrong cylinder parameter");
  require(world.references.size() == 1, "wrong reference count");
  require(world.references[0].translationCm[1] == 5.52, "wrong translation");
  require(world.references[0].rotationDegrees[1] == 90, "wrong rotation");
  std::istringstream hierarchy(R"(*MATC /AIR*.B
880101,0,940621,190801
*MATF  6,1,.129E-02,7.2,14.4,30050,0
**
*GEOM /BEA*.B
890101,0,931220,214701
*MATS  2,AIR*,AIR*
*SHAP  7,CYL1,0,360,0,8,-10,10
**
*GEOM /BEA*/MSK1.B
890101,0,931220,214701
*MATS  2,AIR*,AIR*
*REPL  1,MSK2
**
*GEOM /BEA*/MSK2.B
890101,0,931220,214701
*MATS  2,AIR*,AIR*
*REFR  6,9,9,9,0,0,0
*DBF MTRX (I3/(4G15.7,G15.7))
 12
 1 2 3 0 -1 0 1 0 0 0 0 1
*SHAP  7,CYL1,0,360,4,5,0,10
**
*GEOM /BEA*/MSK2/INNR.B
890101,0,931220,214701
*MATS  2,AIR*,AIR*
*SHAP  7,CYL1,0,360,4,4.5,0,10
**
)");
  const auto hierarchyDatabase =
      delphi_edm4hep::geometry::CargoDatabase::read(hierarchy, "hierarchy");
  const auto hierarchyModel =
      delphi_edm4hep::geometry::GeometryModel::fromCargo(hierarchyDatabase,
                                                         "hierarchy");
  const auto *beam = hierarchyModel.findNode("/BEA*.B");
  const auto *mask1 = hierarchyModel.findNode("/BEA*/MSK1.B");
  require(beam != nullptr && mask1 != nullptr, "hierarchy lookup failed");
  require(hierarchyModel.childrenOf(beam->path).size() == 2,
          "direct child lookup failed");
  require(hierarchyModel.replacementTarget(*mask1)->name == "MSK2",
          "short replacement resolution failed");
  require(hierarchyModel.shapeDefinition(*mask1)->shapes.size() == 1,
          "replacement shape definition was not inherited");
  const auto *mask2 = hierarchyModel.findNode("/BEA*/MSK2.B");
  require(mask2 != nullptr && mask2->references.size() == 1,
          "MTRX transform was not parsed");
  require(mask2->references[0].hasRotationMatrix,
          "MTRX did not take precedence over REFR");
  require(mask2->references[0].translationCm[2] == 3 &&
              mask2->references[0].rotationMatrix[1] == -1,
          "MTRX payload was decoded incorrectly");

  std::istringstream malformed(R"(*GEOM /BROKEN.B
890101,0,931220,214701
*SHAP  7,BRIK,1,2,3
**
)");
  bool rejected = false;
  try {
    const auto broken =
        delphi_edm4hep::geometry::CargoDatabase::read(malformed, "broken");
    static_cast<void>(
        delphi_edm4hep::geometry::GeometryModel::fromCargo(broken, "broken"));
  } catch (const std::runtime_error &error) {
    require(std::string(error.what()).find("declares 7 words") !=
                std::string::npos,
            "malformed payload failed for the wrong reason");
    rejected = true;
  }
  require(rejected, "malformed typed payload was accepted");
}
