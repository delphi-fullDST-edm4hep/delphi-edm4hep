#include "delphi_edm4hep/Geometry/CargoDatabase.h"
#include "delphi_edm4hep/Geometry/GdmlBeamPipeWriter.h"
#include "delphi_edm4hep/Geometry/GdmlDetectorWriter.h"
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

std::size_t occurrences(const std::string &text, std::string_view pattern) {
  std::size_t count{};
  for (auto offset = text.find(pattern); offset != std::string::npos;
       offset = text.find(pattern, offset + pattern.size())) {
    ++count;
  }
  return count;
}

} // namespace

int main() {
  std::istringstream input(R"(*MATC /AIR*.B
880101,0,940621,190801
*MATF  6,1,.129E-02,7.2,14.4,30050,0
**
*MATC /BPAV.B
880101,0,940621,190801
*MATF  6,1,.01,7,14,100,200
**
*MATC /VACU.B
880101,0,940621,190801
*MATF  6,0,0,1,1,0,0
**
*MATC /TUN*.B
880101,0,940621,190801
*MATF  6,1,19.3,74,183.84,.35,10
**
*GEOM /DELF.B
890101,0,931220,214701
*MATS  2,AIR*,AIR*
*SHAP  7,CYL1,0,360,0,680,-585,585
**
*GEOM /BEA*.B
890101,0,931220,214701
*MATS  2,BPAV,AIR*
*SHA1  7,CYL1,0,360,0,8,-20,-10
*SHAP  7,CYL1,0,360,0,8,-10,20
**
*GEOM /BEA*/PIPE.B
890101,0,931220,214701
*MATS  2,VACU,VACU
*SHAP  9,CYL3,0,360,-10,10,0,4,0,5
**
*GEOM /BEA*/VPIC.B
890101,0,931220,214701
*MATS  2,VACU,VACU
*SHAP  7,CYL1,0,360,0,6,-30,30
**
*GEOM /BEA*/VPIC/MSK1.B
890101,0,931220,214701
*MATS  2,VACU,VACU
*REFR  6,0,0,-21,180,180,90
*REPL  1,MSK2
**
*GEOM /BEA*/VPIC/MSK2.B
890101,0,931220,214701
*MATS  2,VACU,VACU
*REFR  6,0,0,21,0,0,0
*SHAP  7,CYL1,0,360,4,5,0,10
**
*GEOM /BEA*/VPIC/MSK2/INC1.B
890101,0,931220,214701
*MATS  2,TUN*,TUN*
*SHAP  4,BRIK,.14,.16,5
**
*GEOM /TPC*.B
890101,0,931220,214701
*MATS  2,BPAV,BPAV
*SHAP  7,CYL1,0,360,10,50,-100,100
**
*GEOM /TPC*/SECT.B
890101,0,931220,214701
*MATS  2,BPAV,BPAV
*REFR  6,0,0,0,30,0,0
*SHAP  14,POL6,1,90,60,20,30,-5,5,-5,5,40,20,-5,5
**
*GEOM /TPC*/AUX*.B
890101,0,931220,214701
*SHAP  1,DUMY
**
*GEOM /TPC*/AUX*/SENS.B
890101,0,931220,214701
*MATS  2,BPAV,BPAV
*DBF MTRX (I3/(4G15.7,G15.7))
 12
 1 2 3 0 -1 0 1 0 0 0 0 1
*SHAP  4,BRIK,1,2,3
**
*GEOM /TPC*/FORB.B
890101,0,931220,214701
*MATS  2,BPAV,BPAV
*SHAP  10,FORB,90,90,20,21,2,-3,3,-2,2
**
*GEOM /TPC*/POL4.B
890101,0,931220,214701
*MATS  2,BPAV,BPAV
*SHAP  10,POL4,4,0,90,30,31,-4,4,-5,5
**
)");
  const auto database =
      delphi_edm4hep::geometry::CargoDatabase::read(input, "fixture");
  const auto model =
      delphi_edm4hep::geometry::GeometryModel::fromCargo(database, "fixture");
  std::ostringstream output;
  delphi_edm4hep::geometry::writeGdmlBeamPipe(output, model, "/DELF.B",
                                              "/BEA*.B", "v94c<&\"");
  const auto gdml = output.str();
  require(gdml.find("delphi_node__BEA__union_1") != std::string::npos,
          "multiple beam-pipe shapes were not combined");
  require(gdml.find("delphi_node__BEA__VPIC_MSK1_INC1") != std::string::npos,
          "replacement children were not instantiated");
  require(gdml.find("<box name=\"delphi_node__BEA__VPIC_MSK1_INC1") !=
              std::string::npos,
          "inherited brick was not rendered");
  require(gdml.find("value=\"1e-25\"") != std::string::npos,
          "DELPHI vacuum sentinel was not mapped for transport");
  require(gdml.find("x=\"-180\"") != std::string::npos &&
              gdml.find("z=\"-90\"") != std::string::npos,
          "DXMATR rotation was not converted to GDML Euler angles");
  require(gdml.find("v94c&lt;&amp;&quot;") != std::string::npos,
          "snapshot identifier was not XML escaped");

  std::ostringstream detectorOutput;
  delphi_edm4hep::geometry::writeGdmlDetector(
      detectorOutput, model,
      {{"/BEA*.B", {}, 0.0, {}},
       {"/TPC*.B", {}, 0.0, {{"/TPC*/SECT.B", "step_tracker_sd", 0.4, 3}}}},
      "/DELF.B", "fixture");
  const auto detector = detectorOutput.str();
  require(detector.find("<tessellated name=\"delphi_node__TPC__SECT") !=
              std::string::npos,
          "POL6 was not rendered as a tessellated solid");
  require(occurrences(detector, "<triangular vertex1=") == 64,
          "tessellated shapes do not have the expected closed facets");
  require(detector.find("<tessellated name=\"delphi_node__TPC__FORB") !=
              std::string::npos,
          "FORB was not rendered as a tessellated solid");
  require(detector.find("<tessellated name=\"delphi_node__TPC__POL4") !=
              std::string::npos,
          "multi-unit POL4 was not rendered as a tessellated solid");
  require(detector.find("auxtype=\"SensDet\" auxvalue=\"step_tracker_sd\"") !=
              std::string::npos,
          "TPC sensing volume was not marked tracker-sensitive");
  require(
      detector.find("auxtype=\"StepLimit\" auxvalue=\"0.4") !=
          std::string::npos,
      "TPC sensing volume did not preserve the DELPHI one-wire-spacing step");
  require(
      detector.find("auxtype=\"CellIDBase\" auxvalue=\"216172786408751104\"") !=
          std::string::npos,
      "TPC sensing volume did not receive its semantic cell ID base");
  require(detector.find("<assembly name=\"delphi_node__TPC__AUX_\">") !=
              std::string::npos,
          "DUMY hierarchy node was not rendered as a GDML assembly");
  require(detector.find("x=\"1\" y=\"2\" z=\"3\" unit=\"cm\"") !=
                  std::string::npos &&
              detector.find("z=\"90\" unit=\"deg\"") != std::string::npos,
          "MTRX placement was not converted to GDML");
}
