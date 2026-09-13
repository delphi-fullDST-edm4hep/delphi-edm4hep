#pragma once

#include <cstddef>
#include <filesystem>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace delphi_edm4hep::geometry {

struct CargoValidity {
  int validFromDate{};
  int validFromTime{};
  int editedDate{};
  int editedTime{};
};

struct CargoField {
  std::string name;
  std::string value;
  std::vector<std::string> continuation;
  std::size_t sourceLine{};
};

struct CargoRecord {
  std::string kind;
  std::string path;
  CargoValidity validity;
  std::vector<CargoField> fields;
  std::size_t sourceLine{};

  const CargoField *findField(std::string_view name) const;
};

class CargoDatabase {
public:
  static CargoDatabase read(std::istream &input,
                            std::string sourceName = "<stream>");
  static CargoDatabase readFile(const std::filesystem::path &path);

  const std::vector<CargoRecord> &records() const { return records_; }
  std::vector<const CargoRecord *> recordsOfKind(std::string_view kind) const;

private:
  std::vector<CargoRecord> records_;
};

} // namespace delphi_edm4hep::geometry
