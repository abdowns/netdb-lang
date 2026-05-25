#pragma once
#include <cstdint>

namespace nql {

enum class Ty : uint8_t {
  Invalid,
  Bool,
  U8, U16, U32, U64,
  I8, I16, I32, I64,
  F64,
  Str,
  IP4,
};

inline bool isIntTy(Ty t) {
  switch (t) {
    case Ty::U8: case Ty::U16: case Ty::U32: case Ty::U64:
    case Ty::I8: case Ty::I16: case Ty::I32: case Ty::I64:
      return true;
    default:
      return false;
  }
}

inline const char* tyName(Ty t) {
  switch (t) {
    case Ty::Bool: return "bool";
    case Ty::U8: return "u8";
    case Ty::U16: return "u16";
    case Ty::U32: return "u32";
    case Ty::U64: return "u64";
    case Ty::I8: return "i8";
    case Ty::I16: return "i16";
    case Ty::I32: return "i32";
    case Ty::I64: return "i64";
    case Ty::F64: return "f64";
    case Ty::Str: return "str";
    case Ty::IP4: return "ip4";
    default: return "?";
  }
}

} // namespace nql
