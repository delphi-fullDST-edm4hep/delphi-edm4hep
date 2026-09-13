#include "delphi_edm4hep/Geometry/GdmlBeamPipeWriter.h"
#include "delphi_edm4hep/Geometry/GdmlDetectorWriter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iterator>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace delphi_edm4hep::geometry {
namespace {

constexpr double pi = 3.14159265358979323846;

struct RenderNode {
  const GeometryNode *record{};
  const GeometryNode *definition{};
  std::string instancePath;
  std::vector<std::size_t> children;
};

struct EulerRotation {
  double x{};
  double y{};
  double z{};
};

struct Point3 {
  double x{};
  double y{};
  double z{};
};

struct Triangle {
  std::size_t first{};
  std::size_t second{};
  std::size_t third{};
};

std::string xmlEscape(std::string_view value) {
  std::string escaped;
  for (const auto character : value) {
    switch (character) {
    case '&':
      escaped += "&amp;";
      break;
    case '<':
      escaped += "&lt;";
      break;
    case '>':
      escaped += "&gt;";
      break;
    case '\"':
      escaped += "&quot;";
      break;
    case '\'':
      escaped += "&apos;";
      break;
    default:
      escaped.push_back(character);
      break;
    }
  }
  return escaped;
}

std::string gdmlName(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const auto character : value) {
    const auto valid = (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9') ||
                       character == '_';
    result.push_back(valid ? character : '_');
  }
  if (result.empty() || (result.front() >= '0' && result.front() <= '9')) {
    result.insert(result.begin(), '_');
  }
  return result;
}

double radians(double degrees) { return degrees * pi / 180.0; }
double degrees(double radiansValue) { return radiansValue * 180.0 / pi; }

std::array<double, 9>
delphiRotationMatrix(const std::array<double, 3> &angles) {
  const auto phi = radians(angles[0]);
  const auto theta = radians(angles[1]);
  const auto psi = radians(angles[2]);
  const auto cphi = std::cos(phi);
  const auto sphi = std::sin(phi);
  const auto ctheta = std::cos(theta);
  const auto stheta = std::sin(theta);
  const auto cpsi = std::cos(psi);
  const auto spsi = std::sin(psi);
  return {
      cpsi * cphi - spsi * ctheta * sphi,
      -spsi * cphi - cpsi * ctheta * sphi,
      stheta * sphi,
      cpsi * sphi + spsi * ctheta * cphi,
      -spsi * sphi + cpsi * ctheta * cphi,
      -stheta * cphi,
      spsi * stheta,
      cpsi * stheta,
      ctheta,
  };
}

EulerRotation gdmlRotation(const std::array<double, 3> &angles) {
  // DXMATR's DELPHI Euler convention is converted to the x-y-z convention
  // used by GDML placements by first constructing the authoritative matrix.
  const auto matrix = delphiRotationMatrix(angles);
  const auto sineY = std::clamp(matrix[2], -1.0, 1.0);
  const auto y = std::asin(sineY);
  const auto cosineY = std::cos(y);
  double x{};
  double z{};
  if (std::abs(cosineY) > 1.0e-12) {
    x = std::atan2(-matrix[5], matrix[8]);
    z = std::atan2(-matrix[1], matrix[0]);
  } else {
    x = std::atan2(sineY * matrix[3], matrix[4]);
  }
  return {degrees(x), degrees(y), degrees(z)};
}

EulerRotation gdmlRotation(const std::array<double, 9> &matrix) {
  const auto sineY = std::clamp(matrix[2], -1.0, 1.0);
  const auto y = std::asin(sineY);
  const auto cosineY = std::cos(y);
  double x{};
  double z{};
  if (std::abs(cosineY) > 1.0e-12) {
    x = std::atan2(-matrix[5], matrix[8]);
    z = std::atan2(-matrix[1], matrix[0]);
  } else {
    x = std::atan2(sineY * matrix[3], matrix[4]);
  }
  return {degrees(x), degrees(y), degrees(z)};
}

bool isDummy(const RenderNode &node) {
  return node.definition->shapes.size() == 1 &&
         node.definition->shapes.front().kind == DelphiShapeKind::Dummy;
}

std::string nodeId(const RenderNode &node) {
  return "delphi_node_" + gdmlName(node.instancePath);
}

std::string shapeId(const RenderNode &node, std::size_t index) {
  return nodeId(node) + "_shape_" + std::to_string(index);
}

std::string solidId(const RenderNode &node) {
  if (node.definition->shapes.size() == 1) {
    return shapeId(node, 0);
  }
  return nodeId(node) + "_union_" +
         std::to_string(node.definition->shapes.size() - 1);
}

std::string vertexId(const RenderNode &node, std::size_t shapeIndex,
                     std::size_t vertexIndex) {
  return shapeId(node, shapeIndex) + "_vertex_" + std::to_string(vertexIndex);
}

std::vector<Point3> poly6Vertices(const RenderNode &node,
                                  const ShapeDefinition &shape) {
  const auto &p = shape.parameters;
  if (p.size() != 13 || std::abs(p[0] - 1.0) > 1.0e-9 || p[2] <= 0 ||
      p[2] >= 180 || p[3] < 0 || p[4] <= p[3] || p[9] <= p[4] || p[10] <= 0 ||
      p[6] <= p[5] || p[8] <= p[7] || p[12] <= p[11]) {
    throw std::runtime_error("invalid or unsupported DELPHI POL6 at " +
                             node.instancePath);
  }
  const auto angle = radians(p[1]);
  const auto cosine = std::cos(angle);
  const auto sine = std::sin(angle);
  const auto tangent = std::tan(radians(p[2]) / 2.0);
  const std::array<double, 3> radius{p[3], p[4], p[9]};
  const std::array<double, 3> halfWidth{p[3] * tangent, p[4] * tangent,
                                        p[10] / 2.0};
  const std::array<double, 3> lowerZ{p[5], p[7], p[11]};
  const std::array<double, 3> upperZ{p[6], p[8], p[12]};
  // DLPOL6 defines three radial edges. The first two follow the sector's
  // azimuthal delimiter planes; the outermost edge has an independent width.
  // Each radial edge therefore contributes its -/+ tangential, lower/upper-Z
  // corners to the twelve-vertex solid.
  std::vector<Point3> vertices;
  vertices.reserve(12);
  for (std::size_t level = 0; level < radius.size(); ++level) {
    for (const auto side : {-1.0, 1.0}) {
      const auto localY = side * halfWidth[level];
      const auto x = radius[level] * cosine - localY * sine;
      const auto y = radius[level] * sine + localY * cosine;
      vertices.push_back({x, y, lowerZ[level]});
      vertices.push_back({x, y, upperZ[level]});
    }
  }
  return vertices;
}

Point3 subtract(const Point3 &left, const Point3 &right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Point3 cross(const Point3 &left, const Point3 &right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

double dot(const Point3 &left, const Point3 &right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

std::vector<Triangle> poly6Triangles(const std::vector<Point3> &vertices) {
  const auto vertex = [](std::size_t level, std::size_t side, std::size_t top) {
    return level * 4 + side * 2 + top;
  };
  std::vector<std::array<std::size_t, 4>> faces{
      {vertex(0, 0, 0), vertex(0, 1, 0), vertex(0, 1, 1), vertex(0, 0, 1)},
      {vertex(2, 0, 0), vertex(2, 1, 0), vertex(2, 1, 1), vertex(2, 0, 1)},
  };
  for (std::size_t level = 0; level < 2; ++level) {
    faces.push_back({vertex(level, 0, 0), vertex(level + 1, 0, 0),
                     vertex(level + 1, 1, 0), vertex(level, 1, 0)});
    faces.push_back({vertex(level, 0, 1), vertex(level + 1, 0, 1),
                     vertex(level + 1, 1, 1), vertex(level, 1, 1)});
    faces.push_back({vertex(level, 0, 0), vertex(level + 1, 0, 0),
                     vertex(level + 1, 0, 1), vertex(level, 0, 1)});
    faces.push_back({vertex(level, 1, 0), vertex(level + 1, 1, 0),
                     vertex(level + 1, 1, 1), vertex(level, 1, 1)});
  }

  Point3 centre;
  for (const auto &point : vertices) {
    centre.x += point.x / static_cast<double>(vertices.size());
    centre.y += point.y / static_cast<double>(vertices.size());
    centre.z += point.z / static_cast<double>(vertices.size());
  }
  std::vector<Triangle> triangles;
  triangles.reserve(faces.size() * 2);
  for (const auto &face : faces) {
    for (const auto indices : {std::array{face[0], face[1], face[2]},
                               std::array{face[0], face[2], face[3]}}) {
      auto triangle = Triangle{indices[0], indices[1], indices[2]};
      const auto &a = vertices[triangle.first];
      const auto &b = vertices[triangle.second];
      const auto &c = vertices[triangle.third];
      const Point3 faceCentre{(a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0,
                              (a.z + b.z + c.z) / 3.0};
      const auto normal = cross(subtract(b, a), subtract(c, a));
      if (dot(normal, subtract(faceCentre, centre)) < 0) {
        std::swap(triangle.second, triangle.third);
      }
      triangles.push_back(triangle);
    }
  }
  return triangles;
}

std::vector<Triangle>
orientedTriangles(const std::vector<Point3> &vertices,
                  const std::vector<std::array<std::size_t, 4>> &faces) {
  Point3 centre;
  for (const auto &point : vertices) {
    centre.x += point.x / static_cast<double>(vertices.size());
    centre.y += point.y / static_cast<double>(vertices.size());
    centre.z += point.z / static_cast<double>(vertices.size());
  }
  std::vector<Triangle> triangles;
  triangles.reserve(faces.size() * 2);
  for (const auto &face : faces) {
    for (const auto indices : {std::array{face[0], face[1], face[2]},
                               std::array{face[0], face[2], face[3]}}) {
      auto triangle = Triangle{indices[0], indices[1], indices[2]};
      const auto &a = vertices[triangle.first];
      const auto &b = vertices[triangle.second];
      const auto &c = vertices[triangle.third];
      const Point3 faceCentre{(a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0,
                              (a.z + b.z + c.z) / 3.0};
      const auto normal = cross(subtract(b, a), subtract(c, a));
      if (dot(normal, subtract(faceCentre, centre)) < 0) {
        std::swap(triangle.second, triangle.third);
      }
      triangles.push_back(triangle);
    }
  }
  return triangles;
}

std::vector<Triangle>
trianglesFromFaces(const std::vector<std::array<std::size_t, 4>> &faces) {
  std::vector<Triangle> triangles;
  triangles.reserve(faces.size() * 2);
  for (const auto &face : faces) {
    triangles.push_back({face[0], face[1], face[2]});
    triangles.push_back({face[0], face[2], face[3]});
  }
  return triangles;
}

std::vector<Point3> fourSidedBoxVertices(const RenderNode &node,
                                         const ShapeDefinition &shape) {
  const auto &p = shape.parameters;
  if (p.size() != 9 || p[4] <= 0 || p[3] <= p[2] || p[6] <= p[5] ||
      p[8] <= p[7]) {
    throw std::runtime_error("invalid DELPHI FORB at " + node.instancePath);
  }
  const auto alpha = radians(p[0]);
  const auto beta = radians(p[1]);
  const auto cosine = std::cos(alpha);
  const auto sine = std::sin(alpha);
  const auto dx = p[4] * std::cos(beta - alpha) / 2.0;
  const auto dy = p[4] * std::sin(beta - alpha) / 2.0;
  return {
      {p[3] * cosine - dx, p[3] * sine + dy, p[6]},
      {p[3] * cosine + dx, p[3] * sine - dy, p[6]},
      {p[3] * cosine - dx, p[3] * sine + dy, p[5]},
      {p[3] * cosine + dx, p[3] * sine - dy, p[5]},
      {p[2] * cosine - dx, p[2] * sine + dy, p[8]},
      {p[2] * cosine + dx, p[2] * sine - dy, p[8]},
      {p[2] * cosine - dx, p[2] * sine + dy, p[7]},
      {p[2] * cosine + dx, p[2] * sine - dy, p[7]},
  };
}

std::vector<std::array<std::size_t, 4>> fourSidedBoxFaces() {
  return {{0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2},
          {4, 5, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
}

struct Polygon4Mesh {
  std::vector<Point3> vertices;
  std::vector<std::array<std::size_t, 4>> faces;
};

Polygon4Mesh polygon4Mesh(const RenderNode &node,
                          const ShapeDefinition &shape) {
  const auto &p = shape.parameters;
  const auto units =
      p.empty() ? 0 : static_cast<std::size_t>(std::llround(p[0]));
  if (p.size() != 9 || units == 0 || std::abs(p[0] - units) > 1.0e-9 ||
      p[2] <= 0 || p[2] >= 180 || units * p[2] > 360.5 || p[3] < 0 ||
      p[4] <= p[3] || p[6] <= p[5] || p[8] <= p[7]) {
    throw std::runtime_error("invalid DELPHI POL4 at " + node.instancePath);
  }
  const auto closed = std::abs(units * p[2] - 360.0) < 0.5;
  const auto boundaries = closed ? units : units + 1;
  const auto cosineHalf = std::cos(radians(p[2]) / 2.0);
  Polygon4Mesh mesh;
  mesh.vertices.reserve(boundaries * 4);
  for (std::size_t index = 0; index < boundaries; ++index) {
    const auto angle =
        radians(p[1] + (static_cast<double>(index) - 0.5) * p[2]);
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    const auto innerRadius = p[3] / cosineHalf;
    const auto outerRadius = p[4] / cosineHalf;
    mesh.vertices.push_back({innerRadius * cosine, innerRadius * sine, p[5]});
    mesh.vertices.push_back({innerRadius * cosine, innerRadius * sine, p[6]});
    mesh.vertices.push_back({outerRadius * cosine, outerRadius * sine, p[7]});
    mesh.vertices.push_back({outerRadius * cosine, outerRadius * sine, p[8]});
  }
  const auto vertex = [](std::size_t boundary, std::size_t offset) {
    return boundary * 4 + offset;
  };
  for (std::size_t index = 0; index < units; ++index) {
    const auto next = closed ? (index + 1) % units : index + 1;
    mesh.faces.push_back(
        {vertex(index, 0), vertex(index, 1), vertex(next, 1), vertex(next, 0)});
    mesh.faces.push_back(
        {vertex(index, 2), vertex(next, 2), vertex(next, 3), vertex(index, 3)});
    mesh.faces.push_back(
        {vertex(index, 0), vertex(next, 0), vertex(next, 2), vertex(index, 2)});
    mesh.faces.push_back(
        {vertex(index, 1), vertex(next, 1), vertex(next, 3), vertex(index, 3)});
  }
  if (!closed) {
    mesh.faces.push_back(
        {vertex(0, 0), vertex(0, 2), vertex(0, 3), vertex(0, 1)});
    mesh.faces.push_back({vertex(units, 0), vertex(units, 1), vertex(units, 3),
                          vertex(units, 2)});
  }
  return mesh;
}

void writeVertices(std::ostream &output, const RenderNode &node,
                   std::size_t shapeIndex,
                   const std::vector<Point3> &vertices) {
  for (std::size_t index = 0; index < vertices.size(); ++index) {
    const auto &point = vertices[index];
    output << "    <position name=\"" << vertexId(node, shapeIndex, index)
           << "\" x=\"" << point.x << "\" y=\"" << point.y << "\" z=\""
           << point.z << "\" unit=\"cm\"/>\n";
  }
}

void writeTessellated(std::ostream &output, const RenderNode &node,
                      std::size_t shapeIndex,
                      const std::vector<Triangle> &triangles) {
  output << "    <tessellated name=\"" << shapeId(node, shapeIndex) << "\">\n";
  for (const auto &triangle : triangles) {
    output << "      <triangular vertex1=\""
           << vertexId(node, shapeIndex, triangle.first) << "\" vertex2=\""
           << vertexId(node, shapeIndex, triangle.second) << "\" vertex3=\""
           << vertexId(node, shapeIndex, triangle.third)
           << "\" type=\"ABSOLUTE\"/>\n";
  }
  output << "    </tessellated>\n";
}

const MaterialAssignment &effectiveMaterial(const RenderNode &node) {
  if (!node.record->materials.empty()) {
    return node.record->materials.front();
  }
  if (!node.definition->materials.empty()) {
    return node.definition->materials.front();
  }
  throw std::runtime_error("DELPHI beam-pipe node has no material: " +
                           node.instancePath);
}

const std::vector<ReferenceTransform> &
effectiveReferences(const RenderNode &node) {
  if (!node.record->references.empty()) {
    return node.record->references;
  }
  return node.definition->references;
}

void writePlacement(std::ostream &output, const std::string &physicalName,
                    const std::string &volumeName,
                    const ReferenceTransform *reference) {
  output << "      <physvol name=\"" << physicalName << "\">\n"
         << "        <volumeref ref=\"" << volumeName << "\"/>\n";
  if (reference != nullptr) {
    const auto rotation = reference->hasRotationMatrix
                              ? gdmlRotation(reference->rotationMatrix)
                              : gdmlRotation(reference->rotationDegrees);
    output << "        <position name=\"" << physicalName << "_position\" x=\""
           << reference->translationCm[0] << "\" y=\""
           << reference->translationCm[1] << "\" z=\""
           << reference->translationCm[2] << "\" unit=\"cm\"/>\n"
           << "        <rotation name=\"" << physicalName << "_rotation\" x=\""
           << rotation.x << "\" y=\"" << rotation.y << "\" z=\"" << rotation.z
           << "\" unit=\"deg\"/>\n";
  }
  output << "      </physvol>\n";
}

void validateRadii(double minimum, double maximum, const RenderNode &node) {
  if (minimum < 0 || maximum <= minimum) {
    throw std::runtime_error("invalid DELPHI beam-pipe radii at " +
                             node.instancePath);
  }
}

void writeShapeDefinitions(std::ostream &output, const RenderNode &node,
                           const ShapeDefinition &shape,
                           std::size_t shapeIndex) {
  switch (shape.kind) {
  case DelphiShapeKind::FourSidedBox:
    writeVertices(output, node, shapeIndex, fourSidedBoxVertices(node, shape));
    return;
  case DelphiShapeKind::Polygon4:
    writeVertices(output, node, shapeIndex, polygon4Mesh(node, shape).vertices);
    return;
  case DelphiShapeKind::Polygon6:
    writeVertices(output, node, shapeIndex, poly6Vertices(node, shape));
    return;
  default:
    return;
  }
}

void writeShape(std::ostream &output, const RenderNode &node,
                const ShapeDefinition &shape, std::size_t index) {
  const auto &parameters = shape.parameters;
  const auto name = shapeId(node, index);
  switch (shape.kind) {
  case DelphiShapeKind::Dummy:
    if (!parameters.empty()) {
      throw std::runtime_error("invalid DELPHI dummy shape at " +
                               node.instancePath);
    }
    return;
  case DelphiShapeKind::FourSidedBox: {
    const auto vertices = fourSidedBoxVertices(node, shape);
    writeTessellated(output, node, index,
                     orientedTriangles(vertices, fourSidedBoxFaces()));
    return;
  }
  case DelphiShapeKind::Polygon4: {
    const auto mesh = polygon4Mesh(node, shape);
    writeTessellated(output, node, index, trianglesFromFaces(mesh.faces));
    return;
  }
  case DelphiShapeKind::Cylinder1:
    if (parameters.size() != 6 || parameters[1] <= parameters[0] ||
        parameters[5] <= parameters[4]) {
      throw std::runtime_error("invalid DELPHI beam-pipe CYL1 at " +
                               node.instancePath);
    }
    validateRadii(parameters[2], parameters[3], node);
    output << "    <polycone name=\"" << name << "\" startphi=\""
           << parameters[0] << "\" deltaphi=\"" << parameters[1] - parameters[0]
           << "\" aunit=\"deg\" lunit=\"cm\">\n"
           << "      <zplane rmin=\"" << parameters[2] << "\" rmax=\""
           << parameters[3] << "\" z=\"" << parameters[4] << "\"/>\n"
           << "      <zplane rmin=\"" << parameters[2] << "\" rmax=\""
           << parameters[3] << "\" z=\"" << parameters[5] << "\"/>\n"
           << "    </polycone>\n";
    return;
  case DelphiShapeKind::Cylinder3:
    if (parameters.size() != 8 || parameters[1] <= parameters[0] ||
        parameters[3] <= parameters[2]) {
      throw std::runtime_error("invalid DELPHI beam-pipe CYL3 at " +
                               node.instancePath);
    }
    validateRadii(parameters[4], parameters[5], node);
    validateRadii(parameters[6], parameters[7], node);
    output << "    <polycone name=\"" << name << "\" startphi=\""
           << parameters[0] << "\" deltaphi=\"" << parameters[1] - parameters[0]
           << "\" aunit=\"deg\" lunit=\"cm\">\n"
           << "      <zplane rmin=\"" << parameters[4] << "\" rmax=\""
           << parameters[5] << "\" z=\"" << parameters[2] << "\"/>\n"
           << "      <zplane rmin=\"" << parameters[6] << "\" rmax=\""
           << parameters[7] << "\" z=\"" << parameters[3] << "\"/>\n"
           << "    </polycone>\n";
    return;
  case DelphiShapeKind::Brick:
    if (parameters.size() != 3 || parameters[0] <= 0 || parameters[1] <= 0 ||
        parameters[2] <= 0) {
      throw std::runtime_error("invalid DELPHI beam-pipe BRIK at " +
                               node.instancePath);
    }
    output << "    <box name=\"" << name << "\" x=\"" << parameters[0]
           << "\" y=\"" << parameters[1] << "\" z=\"" << parameters[2]
           << "\" lunit=\"cm\"/>\n";
    return;
  case DelphiShapeKind::Polygon6: {
    const auto vertices = poly6Vertices(node, shape);
    writeTessellated(output, node, index, poly6Triangles(vertices));
    return;
  }
  default:
    throw std::runtime_error("unsupported DELPHI beam-pipe shape " +
                             std::string(shapeKindName(shape.kind)) + " at " +
                             node.instancePath);
  }
}

std::vector<RenderNode> buildRenderTree(const GeometryModel &model,
                                        const GeometryNode &root) {
  std::vector<RenderNode> nodes;
  std::unordered_set<std::string> active;
  std::function<std::size_t(const GeometryNode &, std::string)> append =
      [&](const GeometryNode &record, std::string instancePath) {
        if (!active.insert(record.path).second) {
          throw std::runtime_error("DELPHI beam-pipe hierarchy cycle at " +
                                   record.path);
        }
        const auto *definition = model.shapeDefinition(record);
        if (definition->shapes.empty()) {
          throw std::runtime_error("DELPHI beam-pipe node has no shape: " +
                                   record.path);
        }
        const auto index = nodes.size();
        nodes.push_back({&record, definition, std::move(instancePath), {}});

        auto children = model.childrenOf(record.path);
        if (children.empty()) {
          if (const auto *replacement = model.replacementTarget(record)) {
            children = model.childrenOf(replacement->path);
          }
        }
        for (const auto *child : children) {
          const auto childIndex =
              append(*child, nodes[index].instancePath + '/' + child->name);
          nodes[index].children.push_back(childIndex);
        }
        active.erase(record.path);
        return index;
      };
  append(root, root.path.substr(0, root.path.size() - 2));
  return nodes;
}

} // namespace

void writeGdmlDetector(std::ostream &output, const GeometryModel &model,
                       const std::vector<GdmlDetectorRoot> &roots,
                       std::string_view worldPath,
                       std::string_view snapshotIdentifier) {
  const auto *world = model.findNode(worldPath);
  if (world == nullptr) {
    throw std::runtime_error("DELPHI GDML world node not found: " +
                             std::string(worldPath));
  }
  if (roots.empty()) {
    throw std::runtime_error("DELPHI GDML detector has no root nodes");
  }
  const auto worldShape = std::find_if(
      world->shapes.begin(), world->shapes.end(),
      [](const auto &candidate) { return candidate.field == "SHAP"; });
  if (worldShape == world->shapes.end() ||
      worldShape->kind != DelphiShapeKind::Cylinder1 ||
      worldShape->parameters.size() != 6 || world->materials.empty()) {
    throw std::runtime_error("DELPHI GDML world definition is invalid");
  }
  const auto &worldParameters = worldShape->parameters;
  if (worldParameters[1] <= worldParameters[0] || worldParameters[2] < 0 ||
      worldParameters[3] <= worldParameters[2] ||
      worldParameters[5] <= worldParameters[4] ||
      std::abs(worldParameters[4] + worldParameters[5]) > 1.0e-9) {
    throw std::runtime_error("DELPHI GDML world bounds are invalid");
  }

  std::vector<RenderNode> nodes;
  std::vector<std::size_t> rootIndices;
  std::unordered_map<std::string, std::string> sensitiveByInstance;
  std::unordered_map<std::string, double> stepLimitByInstance;
  std::unordered_map<std::string, std::uint64_t> cellIDByInstance;
  std::unordered_map<std::uint8_t, std::uint32_t> nextSensorBySubsystem;
  for (const auto &root : roots) {
    const auto *record = model.findNode(root.path);
    if (record == nullptr) {
      throw std::runtime_error("DELPHI GDML detector root not found: " +
                               root.path);
    }
    auto tree = buildRenderTree(model, *record);
    const auto offset = nodes.size();
    for (auto &node : tree) {
      for (auto &child : node.children) {
        child += offset;
      }
    }
    rootIndices.push_back(offset);
    if (!root.sensitiveDetector.empty()) {
      sensitiveByInstance.emplace(tree.front().instancePath,
                                  root.sensitiveDetector);
    }
    if (root.maximumStepCm > 0) {
      stepLimitByInstance.emplace(tree.front().instancePath,
                                  root.maximumStepCm);
    }
    for (const auto &annotation : root.descendants) {
      std::size_t matches{};
      for (const auto &node : tree) {
        if (node.definition->path != annotation.path) {
          continue;
        }
        ++matches;
        if (!annotation.sensitiveDetector.empty()) {
          sensitiveByInstance.emplace(node.instancePath,
                                      annotation.sensitiveDetector);
        }
        if (annotation.maximumStepCm > 0) {
          stepLimitByInstance.emplace(node.instancePath,
                                      annotation.maximumStepCm);
        }
        if (annotation.cellIDSubsystem != 0) {
          auto &sensor = nextSensorBySubsystem[annotation.cellIDSubsystem];
          ++sensor;
          const auto cellIDBase =
              (static_cast<std::uint64_t>(annotation.cellIDSubsystem) << 56U) |
              (static_cast<std::uint64_t>(sensor) << 32U);
          cellIDByInstance.emplace(node.instancePath, cellIDBase);
        }
      }
      if (matches == 0) {
        throw std::runtime_error("DELPHI GDML annotation target not found: " +
                                 annotation.path);
      }
    }
    nodes.insert(nodes.end(), std::make_move_iterator(tree.begin()),
                 std::make_move_iterator(tree.end()));
  }
  std::set<std::string> materialNames{world->materials.front().inner};
  for (const auto &node : nodes) {
    if (isDummy(node)) {
      continue;
    }
    materialNames.insert(effectiveMaterial(node).inner);
  }

  output << std::setprecision(17)
         << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         << "<gdml xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
            "xsi:noNamespaceSchemaLocation=\"http://service-spi.web.cern.ch/"
            "service-spi/app/releases/GDML/schema/gdml.xsd\">\n"
         << "  <define>\n";
  for (const auto &node : nodes) {
    if (isDummy(node)) {
      continue;
    }
    for (std::size_t index = 0; index < node.definition->shapes.size();
         ++index) {
      writeShapeDefinitions(output, node, node.definition->shapes[index],
                            index);
    }
  }
  output << "  </define>\n"
         << "  <materials>\n";
  for (const auto &name : materialNames) {
    const auto *material = model.findMaterial(name);
    if (material == nullptr || material->atomicNumber <= 0 ||
        material->atomicWeightGramPerMole <= 0) {
      throw std::runtime_error("invalid DELPHI beam-pipe material: " + name);
    }
    auto density = material->densityGramPerCm3;
    if (density == 0 && name == "VACU") {
      // MATF uses zero as the DELPHI vacuum sentinel. Geant4 requires a
      // positive density, so retain its H-like Z/A and use a transport vacuum.
      density = 1.0e-25;
    }
    if (density <= 0) {
      throw std::runtime_error("non-positive DELPHI material density: " + name);
    }
    const auto id = gdmlName(name);
    output << "    <element name=\"delphi_element_" << id << "\" formula=\""
           << xmlEscape(name) << "\" Z=\"" << material->atomicNumber << "\">\n"
           << "      <atom unit=\"g/mole\" value=\""
           << material->atomicWeightGramPerMole << "\"/>\n"
           << "    </element>\n"
           << "    <material name=\"delphi_material_" << id << "\" state=\""
           << (density < 0.01 ? "gas" : "solid") << "\">\n"
           << "      <D unit=\"g/cm3\" value=\"" << density << "\"/>\n"
           << "      <fraction n=\"1\" ref=\"delphi_element_" << id << "\"/>\n"
           << "    </material>\n";
  }
  output << "  </materials>\n"
         << "  <solids>\n";

  output << "    <tube name=\"delphi_world_solid\" rmin=\""
         << worldParameters[2] << "\" rmax=\"" << worldParameters[3]
         << "\" z=\"" << worldParameters[5] - worldParameters[4]
         << "\" startphi=\"" << worldParameters[0] << "\" deltaphi=\""
         << worldParameters[1] - worldParameters[0]
         << "\" aunit=\"deg\" lunit=\"cm\"/>\n";
  for (const auto &node : nodes) {
    if (isDummy(node)) {
      continue;
    }
    for (std::size_t index = 0; index < node.definition->shapes.size();
         ++index) {
      writeShape(output, node, node.definition->shapes[index], index);
    }
    for (std::size_t index = 1; index < node.definition->shapes.size();
         ++index) {
      output << "    <union name=\"" << nodeId(node) << "_union_" << index
             << "\">\n"
             << "      <first ref=\""
             << (index == 1
                     ? shapeId(node, 0)
                     : nodeId(node) + "_union_" + std::to_string(index - 1))
             << "\"/>\n"
             << "      <second ref=\"" << shapeId(node, index) << "\"/>\n"
             << "    </union>\n";
    }
  }
  output << "  </solids>\n"
         << "  <structure>\n";

  for (auto node = nodes.rbegin(); node != nodes.rend(); ++node) {
    if (isDummy(*node)) {
      output << "    <assembly name=\"" << nodeId(*node) << "\">\n";
    } else {
      const auto material = effectiveMaterial(*node).inner;
      output << "    <volume name=\"" << nodeId(*node) << "\">\n"
             << "      <materialref ref=\"delphi_material_"
             << gdmlName(material) << "\"/>\n"
             << "      <solidref ref=\"" << solidId(*node) << "\"/>\n";
      if (const auto sensitive = sensitiveByInstance.find(node->instancePath);
          sensitive != sensitiveByInstance.end()) {
        output << "      <auxiliary auxtype=\"SensDet\" auxvalue=\""
               << xmlEscape(sensitive->second) << "\"/>\n";
      }
      if (const auto limit = stepLimitByInstance.find(node->instancePath);
          limit != stepLimitByInstance.end()) {
        output << "      <auxiliary auxtype=\"StepLimit\" auxvalue=\""
               << limit->second << "\" auxunit=\"cm\"/>\n";
      }
      if (const auto cellID = cellIDByInstance.find(node->instancePath);
          cellID != cellIDByInstance.end()) {
        output << "      <auxiliary auxtype=\"CellIDBase\" auxvalue=\""
               << cellID->second << "\"/>\n";
      }
    }
    for (const auto childIndex : node->children) {
      const auto &child = nodes[childIndex];
      const auto &references = effectiveReferences(child);
      if (references.empty()) {
        writePlacement(output, nodeId(child) + "_placement", nodeId(child),
                       nullptr);
      } else {
        for (std::size_t index = 0; index < references.size(); ++index) {
          writePlacement(output,
                         nodeId(child) + "_placement_" + std::to_string(index),
                         nodeId(child), &references[index]);
        }
      }
    }
    output << (isDummy(*node) ? "    </assembly>\n" : "    </volume>\n");
  }

  const auto worldMaterial = world->materials.front().inner;
  output << "    <volume name=\"delphi_world\">\n"
         << "      <materialref ref=\"delphi_material_"
         << gdmlName(worldMaterial) << "\"/>\n"
         << "      <solidref ref=\"delphi_world_solid\"/>\n";
  for (const auto rootIndex : rootIndices) {
    const auto &root = nodes[rootIndex];
    const auto &rootReferences = effectiveReferences(root);
    if (rootReferences.empty()) {
      writePlacement(output, nodeId(root) + "_placement", nodeId(root),
                     nullptr);
    } else {
      for (std::size_t index = 0; index < rootReferences.size(); ++index) {
        writePlacement(output,
                       nodeId(root) + "_placement_" + std::to_string(index),
                       nodeId(root), &rootReferences[index]);
      }
    }
  }
  if (!snapshotIdentifier.empty()) {
    output << "      <auxiliary auxtype=\"DELPHI_CARGO_SOURCE\" auxvalue=\""
           << xmlEscape(snapshotIdentifier) << "\"/>\n";
  }
  output << "    </volume>\n"
         << "  </structure>\n"
         << "  <setup name=\"DELPHI_NATIVE\" version=\"1.0\">\n"
         << "    <world ref=\"delphi_world\"/>\n"
         << "  </setup>\n"
         << "</gdml>\n";
  if (!output) {
    throw std::runtime_error("failed while writing DELPHI detector GDML");
  }
}

void writeGdmlBeamPipe(std::ostream &output, const GeometryModel &model,
                       std::string_view worldPath,
                       std::string_view beamPipePath,
                       std::string_view snapshotIdentifier) {
  writeGdmlDetector(output, model,
                    {{std::string(beamPipePath), std::string{}, 0.0, {}}},
                    worldPath, snapshotIdentifier);
}

} // namespace delphi_edm4hep::geometry
