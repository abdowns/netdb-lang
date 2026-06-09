#pragma once
#include <cstdint>
#include <string>
#include <string_view>

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

inline bool isIntTy(Ty t) { return t >= Ty::U8 && t <= Ty::I64; }
inline bool isUnsignedTy(Ty t) { return t >= Ty::U8 && t <= Ty::U64; }
inline bool isNumericTy(Ty t) { return isIntTy(t) || t == Ty::F64; }

inline uint32_t tySize(Ty t) {
  switch (t) {
    case Ty::Bool: case Ty::U8: case Ty::I8: return 1;
    case Ty::U16: case Ty::I16: return 2;
    case Ty::U32: case Ty::I32: case Ty::IP4: return 4;
    case Ty::U64: case Ty::I64: case Ty::F64: return 8;
    case Ty::Str: return 16;
    default: return 0;
  }
}

inline uint32_t tyAlign(Ty t) { return t == Ty::Str ? 8 : tySize(t); }

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
    default: return "<invalid>";
  }
}

inline Ty tyFromName(std::string_view s) {
  if (s == "bool") return Ty::Bool;
  if (s == "u8") return Ty::U8;
  if (s == "u16") return Ty::U16;
  if (s == "u32") return Ty::U32;
  if (s == "u64" || s == "uint" || s == "time") return Ty::U64;
  if (s == "i8") return Ty::I8;
  if (s == "i16") return Ty::I16;
  if (s == "i32") return Ty::I32;
  if (s == "i64" || s == "int") return Ty::I64;
  if (s == "f64" || s == "float") return Ty::F64;
  if (s == "str") return Ty::Str;
  if (s == "ip4" || s == "ip") return Ty::IP4;
  return Ty::Invalid;
}

// widens to 64 bits; unsigned only if a u64 operand is involved
struct Promo {
  bool isFloat = false;
  bool isUnsigned = false;
};

inline Promo promote(Ty a, Ty b) {
  Promo p;
  p.isFloat = (a == Ty::F64 || b == Ty::F64);
  p.isUnsigned = !p.isFloat && (a == Ty::U64 || b == Ty::U64);
  return p;
}

inline std::string ipToString(uint32_t ip) {
  return std::to_string((ip >> 24) & 0xff) + "." + std::to_string((ip >> 16) & 0xff) +
         "." + std::to_string((ip >> 8) & 0xff) + "." + std::to_string(ip & 0xff);
}

} // namespace nql
