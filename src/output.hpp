#pragma once
#include <cstdint>
#include <ostream>
#include <vector>

#include "reflect.hpp"

namespace nql {

class RecordSet;

enum class OutFormat { Json, Csv, Table };

// empty fields means all fields, in declaration order
void writeRecords(std::ostream& os, const RecordSet& rs, const std::vector<uint64_t>& indices,
                  const std::vector<const FieldInfo*>& fields, OutFormat fmt);

void writeJsonRecord(std::ostream& os, const uint8_t* rec,
                     const std::vector<const FieldInfo*>& fields);

} // namespace nql
