#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "types.hpp"

namespace nql {

struct FieldInfo {
  std::string name;
  Ty ty = Ty::Invalid;
  uint32_t index = 0;
  uint32_t offset = 0;
  uint32_t size = 0;
  uint32_t align = 0;
};

struct Schema {
  std::string name;
  std::vector<FieldInfo> fields;
  uint32_t size = 0;
  uint32_t align = 1;

  void layout();
  const FieldInfo* field(std::string_view n) const;
};

struct StrRef {
  const char* ptr;
  uint64_t len;
};
static_assert(sizeof(StrRef) == 16);

uint64_t loadUInt(const uint8_t* rec, const FieldInfo& f); // zero extended
int64_t loadSInt(const uint8_t* rec, const FieldInfo& f);  // sign extended
double loadF64(const uint8_t* rec, const FieldInfo& f);
StrRef loadStr(const uint8_t* rec, const FieldInfo& f);

void storeUInt(uint8_t* rec, const FieldInfo& f, uint64_t v);
void storeF64(uint8_t* rec, const FieldInfo& f, double v);
void storeStr(uint8_t* rec, const FieldInfo& f, StrRef s);

} // namespace nql
