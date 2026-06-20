#include "output.hpp"

#include <algorithm>
#include <string>

#include "data.hpp"

namespace nql {
namespace {

std::string fieldToString(const uint8_t* rec, const FieldInfo& f) {
  switch (f.ty) {
    case Ty::Bool: return loadUInt(rec, f) ? "true" : "false";
    case Ty::F64: return std::to_string(loadF64(rec, f));
    case Ty::Str: {
      StrRef s = loadStr(rec, f);
      return std::string(s.ptr, s.len);
    }
    case Ty::IP4: return ipToString((uint32_t)loadUInt(rec, f));
    default:
      return isUnsignedTy(f.ty) ? std::to_string(loadUInt(rec, f))
                                : std::to_string(loadSInt(rec, f));
  }
}

} // namespace

void writeJsonRecord(std::ostream& os, const uint8_t* rec,
                     const std::vector<const FieldInfo*>& fields) {
  os << '{';
  for (size_t i = 0; i < fields.size(); i++) {
    const FieldInfo& f = *fields[i];
    if (i) os << ", ";
    os << '"' << f.name << "\": ";
    if (f.ty == Ty::Str || f.ty == Ty::IP4)
      os << '"' << fieldToString(rec, f) << '"';
    else
      os << fieldToString(rec, f);
  }
  os << "}\n";
}

void writeRecords(std::ostream& os, const RecordSet& rs, const std::vector<uint64_t>& indices,
                  const std::vector<const FieldInfo*>& fieldsIn, OutFormat fmt) {
  std::vector<const FieldInfo*> fields = fieldsIn;
  if (fields.empty())
    for (const auto& f : rs.schema().fields) fields.push_back(&f);

  switch (fmt) {
    case OutFormat::Json:
      for (uint64_t idx : indices) writeJsonRecord(os, rs.at(idx), fields);
      return;

    case OutFormat::Csv: {
      for (size_t i = 0; i < fields.size(); i++) {
        if (i) os << ',';
        os << fields[i]->name;
      }
      os << '\n';
      for (uint64_t idx : indices) {
        const uint8_t* rec = rs.at(idx);
        for (size_t i = 0; i < fields.size(); i++) {
          if (i) os << ',';
          os << fieldToString(rec, *fields[i]);
        }
        os << '\n';
      }
      return;
    }

    case OutFormat::Table: {
      // two passes: measure column widths, then print
      std::vector<size_t> width(fields.size());
      for (size_t i = 0; i < fields.size(); i++) width[i] = fields[i]->name.size();
      std::vector<std::vector<std::string>> rows;
      rows.reserve(indices.size());
      for (uint64_t idx : indices) {
        const uint8_t* rec = rs.at(idx);
        std::vector<std::string> row;
        for (size_t i = 0; i < fields.size(); i++) {
          row.push_back(fieldToString(rec, *fields[i]));
          width[i] = std::max(width[i], row.back().size());
        }
        rows.push_back(std::move(row));
      }
      auto rule = [&] {
        for (size_t i = 0; i < fields.size(); i++) {
          os << '+' << std::string(width[i] + 2, '-');
        }
        os << "+\n";
      };
      rule();
      for (size_t i = 0; i < fields.size(); i++) {
        os << "| " << fields[i]->name
           << std::string(width[i] - fields[i]->name.size() + 1, ' ');
      }
      os << "|\n";
      rule();
      for (const auto& row : rows) {
        for (size_t i = 0; i < fields.size(); i++)
          os << "| " << row[i] << std::string(width[i] - row[i].size() + 1, ' ');
        os << "|\n";
      }
      rule();
      return;
    }
  }
}

} // namespace nql
