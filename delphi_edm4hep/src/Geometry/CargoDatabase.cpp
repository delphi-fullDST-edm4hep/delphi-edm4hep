#include "delphi_edm4hep/Geometry/CargoDatabase.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace delphi_edm4hep::geometry {
namespace {

std::string trim(std::string_view value) {
  const auto first =
      std::find_if_not(value.begin(), value.end(),
                       [](unsigned char c) { return std::isspace(c); });
  const auto last =
      std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c);
      }).base();
  if (first >= last) {
    return {};
  }
  return {first, last};
}

[[noreturn]] void parseError(const std::string &source, std::size_t line,
                             const std::string &message) {
  throw std::runtime_error(source + ":" + std::to_string(line) + ": " +
                           message);
}

bool recordHeader(std::string_view line, std::string &kind, std::string &path) {
  if (line.empty() || line.front() != '*' || line.starts_with("**")) {
    return false;
  }

  const auto separator = line.find_first_of(" \t", 1);
  if (separator == std::string_view::npos) {
    return false;
  }
  const auto candidatePath = trim(line.substr(separator));
  if (candidatePath.empty() || candidatePath.front() != '/') {
    return false;
  }

  kind = trim(line.substr(1, separator - 1));
  path = candidatePath;
  return !kind.empty();
}

CargoValidity parseValidity(std::string line, const std::string &source,
                            std::size_t lineNumber) {
  std::replace(line.begin(), line.end(), ',', ' ');
  CargoValidity validity;
  std::istringstream values(line);
  if (!(values >> validity.validFromDate >> validity.validFromTime >>
        validity.editedDate >> validity.editedTime)) {
    parseError(source, lineNumber,
               "expected four comma-separated validity integers");
  }
  std::string extra;
  if (values >> extra) {
    parseError(source, lineNumber, "unexpected data after validity interval");
  }
  return validity;
}

CargoField parseField(std::string_view line, std::size_t lineNumber) {
  const auto separator = line.find_first_of(" \t", 1);
  CargoField field;
  field.name =
      trim(line.substr(1, separator == std::string_view::npos ? line.size() - 1
                                                              : separator - 1));
  field.value = separator == std::string_view::npos
                    ? std::string{}
                    : trim(line.substr(separator));
  field.sourceLine = lineNumber;
  return field;
}

} // namespace

const CargoField *CargoRecord::findField(std::string_view name) const {
  const auto found =
      std::find_if(fields.begin(), fields.end(),
                   [name](const auto &field) { return field.name == name; });
  return found == fields.end() ? nullptr : &*found;
}

CargoDatabase CargoDatabase::read(std::istream &input, std::string sourceName) {
  CargoDatabase database;
  CargoRecord *current = nullptr;
  CargoField *currentField = nullptr;
  bool expectsValidity = false;
  std::string line;
  std::size_t lineNumber = 0;

  while (std::getline(input, line)) {
    ++lineNumber;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    std::string kind;
    std::string path;
    if (recordHeader(line, kind, path)) {
      if (current != nullptr) {
        parseError(sourceName, lineNumber,
                   "new record before previous record terminator");
      }
      database.records_.push_back(
          {std::move(kind), std::move(path), {}, {}, lineNumber});
      current = &database.records_.back();
      currentField = nullptr;
      expectsValidity = true;
      continue;
    }

    if (current == nullptr) {
      continue;
    }
    if (expectsValidity) {
      current->validity = parseValidity(line, sourceName, lineNumber);
      expectsValidity = false;
      continue;
    }
    if (line.starts_with("**")) {
      current = nullptr;
      currentField = nullptr;
      continue;
    }
    if (!line.empty() && line.front() == '*') {
      current->fields.push_back(parseField(line, lineNumber));
      currentField = &current->fields.back();
      continue;
    }
    if (currentField == nullptr) {
      if (!trim(line).empty()) {
        parseError(sourceName, lineNumber,
                   "record data appears before its first field");
      }
      continue;
    }
    currentField->continuation.push_back(line);
  }

  if (current != nullptr) {
    parseError(sourceName, lineNumber,
               "unterminated record beginning on line " +
                   std::to_string(current->sourceLine));
  }
  return database;
}

CargoDatabase CargoDatabase::readFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open DELPHI CARGO database: " +
                             path.string());
  }
  return read(input, path.string());
}

std::vector<const CargoRecord *>
CargoDatabase::recordsOfKind(std::string_view kind) const {
  std::vector<const CargoRecord *> selected;
  for (const auto &record : records_) {
    if (record.kind == kind) {
      selected.push_back(&record);
    }
  }
  return selected;
}

} // namespace delphi_edm4hep::geometry
