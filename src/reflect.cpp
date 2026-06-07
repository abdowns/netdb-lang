#include "reflect.hpp"

#include <cstring>

namespace nql {

static uint32_t alignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

void Schema::layout() {
  uint32_t off = 0;
  align = 1;
  uint32_t idx = 0;
  for (auto& f : fields) {
    f.size = tySize(f.ty);
    f.align = tyAlign(f.ty);
    f.index = idx++;
    off = alignUp(off, f.align);
    f.offset = off;
    off += f.size;
    if (f.align > align) align = f.align;
  }
  size = alignUp(off, align);
}

const FieldInfo* Schema::field(std::string_view n) const {
  for (const auto& f : fields)
    if (f.name == n) return &f;
  return nullptr;
}

uint64_t loadUInt(const uint8_t* rec, const FieldInfo& f) {
  const uint8_t* p = rec + f.offset;
  switch (f.size) {
    case 1: return *p;
    case 2: { uint16_t v; std::memcpy(&v, p, 2); return v; }
    case 4: { uint32_t v; std::memcpy(&v, p, 4); return v; }
    default: { uint64_t v; std::memcpy(&v, p, 8); return v; }
  }
}

int64_t loadSInt(const uint8_t* rec, const FieldInfo& f) {
  const uint8_t* p = rec + f.offset;
  switch (f.size) {
    case 1: { int8_t v; std::memcpy(&v, p, 1); return v; }
    case 2: { int16_t v; std::memcpy(&v, p, 2); return v; }
    case 4: { int32_t v; std::memcpy(&v, p, 4); return v; }
    default: { int64_t v; std::memcpy(&v, p, 8); return v; }
  }
}

double loadF64(const uint8_t* rec, const FieldInfo& f) {
  double v;
  std::memcpy(&v, rec + f.offset, 8);
  return v;
}

StrRef loadStr(const uint8_t* rec, const FieldInfo& f) {
  StrRef s;
  std::memcpy(&s, rec + f.offset, sizeof(StrRef));
  return s;
}

void storeUInt(uint8_t* rec, const FieldInfo& f, uint64_t v) {
  std::memcpy(rec + f.offset, &v, f.size);
}

void storeF64(uint8_t* rec, const FieldInfo& f, double v) {
  std::memcpy(rec + f.offset, &v, 8);
}

void storeStr(uint8_t* rec, const FieldInfo& f, StrRef s) {
  std::memcpy(rec + f.offset, &s, sizeof(StrRef));
}

} // namespace nql
