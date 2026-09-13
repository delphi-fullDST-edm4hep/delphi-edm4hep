#include "delphi_edm4hep/Geometry/GeometryModel.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace delphi_edm4hep::geometry {
namespace {

std::string trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

[[noreturn]] void modelError(const std::string &source, std::size_t line,
                             const std::string &message) {
  throw std::runtime_error(source + ":" + std::to_string(line) + ": " +
                           message);
}

std::vector<std::string> payload(const CargoField &field,
                                 const std::string &source) {
  auto logicalValue = field.value;
  for (const auto &continuation : field.continuation) {
    const auto next = trim(continuation);
    if (next.empty()) {
      continue;
    }
    if (!logicalValue.empty()) {
      logicalValue.push_back(',');
    }
    logicalValue += next;
  }
  std::vector<std::string> tokens;
  std::size_t begin = 0;
  while (begin <= logicalValue.size()) {
    const auto comma = logicalValue.find(',', begin);
    const auto end = comma == std::string::npos ? logicalValue.size() : comma;
    tokens.push_back(
        trim(std::string_view(logicalValue).substr(begin, end - begin)));
    if (comma == std::string::npos) {
      break;
    }
    begin = comma + 1;
  }
  if (tokens.empty() || tokens.front().empty()) {
    modelError(source, field.sourceLine,
               field.name + " has no payload-length word");
  }

  std::size_t count = 0;
  const auto *first = tokens.front().data();
  const auto *last = first + tokens.front().size();
  const auto parsed = std::from_chars(first, last, count);
  if (parsed.ec != std::errc{} || parsed.ptr != last) {
    modelError(source, field.sourceLine,
               field.name + " has an invalid payload length");
  }
  tokens.erase(tokens.begin());
  if (tokens.size() != count) {
    modelError(source, field.sourceLine,
               field.name + " declares " + std::to_string(count) +
                   " words but contains " + std::to_string(tokens.size()));
  }
  return tokens;
}

std::vector<std::string> dbfPayload(const CargoField &field,
                                    std::string_view expectedName,
                                    const std::string &source) {
  const auto separator = field.value.find_first_of(" \t(");
  const auto name = trim(std::string_view(field.value).substr(0, separator));
  if (name != expectedName) {
    return {};
  }
  std::vector<std::string> tokens;
  for (const auto &line : field.continuation) {
    auto normalized = line;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream words(normalized);
    for (std::string token; words >> token;) {
      tokens.push_back(std::move(token));
    }
  }
  if (tokens.empty()) {
    modelError(source, field.sourceLine,
               "DBF " + std::string(expectedName) + " has no word count");
  }
  std::size_t count = 0;
  const auto *first = tokens.front().data();
  const auto *last = first + tokens.front().size();
  const auto parsed = std::from_chars(first, last, count);
  if (parsed.ec != std::errc{} || parsed.ptr != last) {
    modelError(source, field.sourceLine,
               "DBF " + std::string(expectedName) +
                   " has an invalid word count");
  }
  tokens.erase(tokens.begin());
  if (tokens.size() != count) {
    modelError(source, field.sourceLine,
               "DBF " + std::string(expectedName) + " declares " +
                   std::to_string(count) + " words but contains " +
                   std::to_string(tokens.size()));
  }
  return tokens;
}

double number(const std::string &token, const CargoField &field,
              const std::string &source) {
  auto normalized = token;
  std::replace(normalized.begin(), normalized.end(), 'd', 'E');
  std::replace(normalized.begin(), normalized.end(), 'D', 'E');
  errno = 0;
  char *end = nullptr;
  const auto result = std::strtod(normalized.c_str(), &end);
  if (errno == ERANGE || end == normalized.c_str() ||
      end != normalized.c_str() + normalized.size() || !std::isfinite(result)) {
    modelError(source, field.sourceLine,
               field.name + " contains a non-numeric word: " + token);
  }
  return result;
}

std::string recordName(std::string_view path) {
  const auto slash = path.find_last_of('/');
  auto name =
      std::string(path.substr(slash == std::string_view::npos ? 0 : slash + 1));
  if (name.ends_with(".B")) {
    name.resize(name.size() - 2);
  }
  return name;
}

DelphiShapeKind shapeKind(const std::string &tag, const CargoField &field,
                          const std::string &source) {
  if (tag == "DUMY")
    return DelphiShapeKind::Dummy;
  if (tag == "FORB")
    return DelphiShapeKind::FourSidedBox;
  if (tag == "POL4")
    return DelphiShapeKind::Polygon4;
  if (tag == "POL6")
    return DelphiShapeKind::Polygon6;
  if (tag == "CYL0")
    return DelphiShapeKind::Cylinder0;
  if (tag == "CYL1")
    return DelphiShapeKind::Cylinder1;
  if (tag == "CYL2")
    return DelphiShapeKind::Cylinder2;
  if (tag == "CYL3")
    return DelphiShapeKind::Cylinder3;
  if (tag == "SPHE")
    return DelphiShapeKind::Sphere;
  if (tag == "PARA")
    return DelphiShapeKind::Paraboloid;
  if (tag == "WED4")
    return DelphiShapeKind::Wedge4;
  if (tag == "BRIK")
    return DelphiShapeKind::Brick;
  if (tag.size() == 4 && tag.starts_with("PL") && tag[2] >= '0' &&
      tag[2] <= '9' && tag[3] >= '0' && tag[3] <= '9') {
    return DelphiShapeKind::PlanePolyhedron;
  }
  modelError(source, field.sourceLine, "unknown DELPHI shape tag: " + tag);
}

std::size_t fixedShapeWords(DelphiShapeKind kind) {
  switch (kind) {
  case DelphiShapeKind::Dummy:
    return 1;
  case DelphiShapeKind::Brick:
  case DelphiShapeKind::Cylinder0:
    return 4;
  case DelphiShapeKind::Cylinder1:
    return 7;
  case DelphiShapeKind::Cylinder2:
  case DelphiShapeKind::Cylinder3:
  case DelphiShapeKind::Wedge4:
    return 9;
  case DelphiShapeKind::FourSidedBox:
  case DelphiShapeKind::Polygon4:
    return 10;
  case DelphiShapeKind::Sphere:
  case DelphiShapeKind::Paraboloid:
    return 12;
  case DelphiShapeKind::Polygon6:
    return 14;
  case DelphiShapeKind::PlanePolyhedron:
    return 0;
  }
  return 0;
}

std::size_t planeShapeWords(const std::string &tag, const CargoField &field,
                            const std::string &source) {
  auto firstPlanes = static_cast<std::size_t>(tag[2] - '0');
  auto secondPlanes = static_cast<std::size_t>(tag[3] - '0');
  if (firstPlanes == 0) {
    firstPlanes = secondPlanes;
    secondPlanes = 0;
  }
  if (firstPlanes < 3 || (secondPlanes != 0 && secondPlanes < 3)) {
    modelError(source, field.sourceLine,
               tag + " has an invalid pair of plane counts");
  }
  const auto pointsPerFace =
      firstPlanes + (secondPlanes > 2 ? secondPlanes - 2 : 0);
  return 1 + 6 * pointsPerFace;
}

bool shapeField(std::string_view name) {
  return name.size() == 4 && name.starts_with("SHA") &&
         (name == "SHAP" || (name[3] >= '1' && name[3] <= '9'));
}

bool referenceField(std::string_view name) {
  return name.size() == 4 && name.starts_with("REF") &&
         (name == "REFR" || (name[3] >= '1' && name[3] <= '9'));
}

std::string withoutBankSuffix(std::string_view path) {
  auto result = std::string(path);
  if (result.ends_with(".B")) {
    result.resize(result.size() - 2);
  }
  return result;
}

std::string replacementPath(const GeometryNode &node,
                            const std::vector<std::string> &replacement) {
  if (replacement.empty()) {
    return {};
  }
  if (replacement.size() == 1) {
    const auto current = withoutBankSuffix(node.path);
    const auto slash = current.find_last_of('/');
    if (slash == std::string::npos) {
      return {};
    }
    return current.substr(0, slash + 1) + replacement.front() + ".B";
  }
  std::string path;
  for (const auto &component : replacement) {
    path.push_back('/');
    path += component;
  }
  path += ".B";
  return path;
}

} // namespace

std::string_view shapeKindName(DelphiShapeKind kind) {
  switch (kind) {
  case DelphiShapeKind::Dummy:
    return "DUMY";
  case DelphiShapeKind::FourSidedBox:
    return "FORB";
  case DelphiShapeKind::Polygon4:
    return "POL4";
  case DelphiShapeKind::Polygon6:
    return "POL6";
  case DelphiShapeKind::Cylinder0:
    return "CYL0";
  case DelphiShapeKind::Cylinder1:
    return "CYL1";
  case DelphiShapeKind::Cylinder2:
    return "CYL2";
  case DelphiShapeKind::Cylinder3:
    return "CYL3";
  case DelphiShapeKind::Sphere:
    return "SPHE";
  case DelphiShapeKind::Paraboloid:
    return "PARA";
  case DelphiShapeKind::Wedge4:
    return "WED4";
  case DelphiShapeKind::Brick:
    return "BRIK";
  case DelphiShapeKind::PlanePolyhedron:
    return "PLNM";
  }
  return "unknown";
}

GeometryModel GeometryModel::fromCargo(const CargoDatabase &database,
                                       std::string sourceName) {
  GeometryModel model;
  for (const auto &record : database.records()) {
    if (record.kind == "MATC") {
      const auto *field = record.findField("MATF");
      if (field == nullptr) {
        continue;
      }
      const auto words = payload(*field, sourceName);
      if (words.size() != 6) {
        modelError(sourceName, field->sourceLine,
                   "MATF must contain six material words");
      }
      MaterialDefinition material;
      material.name = recordName(record.path);
      material.sourceLine = field->sourceLine;
      const auto radiationFlag = number(words[0], *field, sourceName);
      if (radiationFlag != 0.0 && radiationFlag != 1.0) {
        modelError(sourceName, field->sourceLine,
                   "MATF radiation-length flag must be zero or one");
      }
      material.radiationLengthProvided = radiationFlag == 1.0;
      material.densityGramPerCm3 = number(words[1], *field, sourceName);
      material.atomicNumber = number(words[2], *field, sourceName);
      material.atomicWeightGramPerMole = number(words[3], *field, sourceName);
      material.radiationLengthCm = number(words[4], *field, sourceName);
      material.interactionLengthCm = number(words[5], *field, sourceName);
      model.materials_.push_back(std::move(material));
      continue;
    }
    if (record.kind != "GEOM") {
      continue;
    }

    GeometryNode node;
    node.path = record.path;
    node.name = recordName(record.path);
    node.sourceLine = record.sourceLine;
    bool hasMatrixTransform = false;
    for (const auto &field : record.fields) {
      if (field.name == "MATS") {
        const auto words = payload(field, sourceName);
        if (words.size() != 2) {
          modelError(sourceName, field.sourceLine,
                     "MATS must contain inner and outer material names");
        }
        node.materials.push_back({words[0], words[1], field.sourceLine});
      } else if (shapeField(field.name)) {
        auto words = payload(field, sourceName);
        if (words.empty()) {
          modelError(sourceName, field.sourceLine,
                     field.name + " has no shape tag");
        }
        const auto tag = trim(words.front());
        const auto kind = shapeKind(tag, field, sourceName);
        const auto expected = kind == DelphiShapeKind::PlanePolyhedron
                                  ? planeShapeWords(tag, field, sourceName)
                                  : fixedShapeWords(kind);
        if (words.size() != expected) {
          modelError(sourceName, field.sourceLine,
                     tag + " expects " + std::to_string(expected) +
                         " words, found " + std::to_string(words.size()));
        }
        ShapeDefinition shape;
        shape.field = field.name;
        shape.tag = tag;
        shape.kind = kind;
        shape.sourceLine = field.sourceLine;
        shape.parameters.reserve(words.size() - 1);
        for (std::size_t index = 1; index < words.size(); ++index) {
          shape.parameters.push_back(number(words[index], field, sourceName));
        }
        node.shapes.push_back(std::move(shape));
      } else if (field.name == "DBF") {
        const auto words = dbfPayload(field, "MTRX", sourceName);
        if (words.empty()) {
          continue;
        }
        if (words.size() != 12) {
          modelError(sourceName, field.sourceLine,
                     "DBF MTRX must contain twelve transform values");
        }
        ReferenceTransform reference;
        reference.field = "MTRX";
        reference.hasRotationMatrix = true;
        reference.sourceLine = field.sourceLine;
        for (std::size_t index = 0; index < 3; ++index) {
          reference.translationCm[index] =
              number(words[index], field, sourceName);
        }
        for (std::size_t index = 0; index < 9; ++index) {
          reference.rotationMatrix[index] =
              number(words[index + 3], field, sourceName);
        }
        node.references.clear();
        node.references.push_back(std::move(reference));
        hasMatrixTransform = true;
      } else if (referenceField(field.name) && !hasMatrixTransform) {
        const auto words = payload(field, sourceName);
        if (words.size() != 6) {
          modelError(sourceName, field.sourceLine,
                     field.name + " must contain six transform values");
        }
        ReferenceTransform reference;
        reference.field = field.name;
        reference.sourceLine = field.sourceLine;
        for (std::size_t index = 0; index < 3; ++index) {
          reference.translationCm[index] =
              number(words[index], field, sourceName);
          reference.rotationDegrees[index] =
              number(words[index + 3], field, sourceName);
        }
        node.references.push_back(std::move(reference));
      } else if (field.name == "REPL") {
        node.replacements.push_back(payload(field, sourceName));
      }
    }
    model.nodes_.push_back(std::move(node));
  }

  std::unordered_set<std::string> materialNames;
  for (const auto &material : model.materials_) {
    if (!materialNames.insert(material.name).second) {
      modelError(sourceName, material.sourceLine,
                 "duplicate MATC material: " + material.name);
    }
  }
  std::unordered_set<std::string> nodePaths;
  for (const auto &node : model.nodes_) {
    if (!nodePaths.insert(node.path).second) {
      modelError(sourceName, node.sourceLine,
                 "duplicate GEOM path: " + node.path);
    }
    for (const auto &assignment : node.materials) {
      if (!materialNames.contains(assignment.inner) ||
          !materialNames.contains(assignment.outer)) {
        modelError(sourceName, assignment.sourceLine,
                   "MATS references an undefined material");
      }
    }
  }
  for (const auto &node : model.nodes_) {
    static_cast<void>(model.shapeDefinition(node));
  }
  return model;
}

const MaterialDefinition *
GeometryModel::findMaterial(std::string_view name) const {
  const auto found = std::find_if(
      materials_.begin(), materials_.end(),
      [name](const auto &material) { return material.name == name; });
  return found == materials_.end() ? nullptr : &*found;
}

const GeometryNode *GeometryModel::findNode(std::string_view path) const {
  const auto found =
      std::find_if(nodes_.begin(), nodes_.end(),
                   [path](const auto &node) { return node.path == path; });
  return found == nodes_.end() ? nullptr : &*found;
}

std::vector<const GeometryNode *>
GeometryModel::childrenOf(std::string_view path) const {
  const auto prefix = withoutBankSuffix(path) + '/';
  std::vector<const GeometryNode *> children;
  for (const auto &candidate : nodes_) {
    if (!candidate.path.starts_with(prefix)) {
      continue;
    }
    const auto remainder =
        std::string_view(candidate.path).substr(prefix.size());
    if (remainder.find('/') == std::string_view::npos) {
      children.push_back(&candidate);
    }
  }
  return children;
}

const GeometryNode *
GeometryModel::replacementTarget(const GeometryNode &node) const {
  if (node.replacements.empty()) {
    return nullptr;
  }
  if (node.replacements.size() != 1) {
    throw std::runtime_error("GEOM node has multiple REPL fields: " +
                             node.path);
  }
  const auto path = replacementPath(node, node.replacements.front());
  auto *target = findNode(path);
  if (target == nullptr && node.replacements.front().size() == 1 &&
      node.path.size() > 1) {
    // DSREPL's compatibility search falls back from a sibling lookup to the
    // first identically named node in a detector tree whose root starts with
    // the same character. Preserve snapshot order to match its traversal.
    const auto rootInitial = node.path[1];
    const auto &name = node.replacements.front().front();
    const auto fallback =
        std::find_if(nodes_.begin(), nodes_.end(), [&](const auto &candidate) {
          return candidate.path.size() > 1 &&
                 candidate.path[1] == rootInitial && candidate.name == name;
        });
    if (fallback != nodes_.end()) {
      target = &*fallback;
    }
  }
  if (target == nullptr) {
    throw std::runtime_error("GEOM replacement target not found for " +
                             node.path + ": " + path);
  }
  return target;
}

const GeometryNode *
GeometryModel::shapeDefinition(const GeometryNode &node) const {
  const GeometryNode *current = &node;
  std::unordered_set<std::string> visited;
  while (const auto *replacement = replacementTarget(*current)) {
    if (!visited.insert(current->path).second) {
      throw std::runtime_error("GEOM replacement cycle at " + current->path);
    }
    current = replacement;
  }
  return current;
}

} // namespace delphi_edm4hep::geometry
