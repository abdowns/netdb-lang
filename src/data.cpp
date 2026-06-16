#include "data.hpp"

#include <random>

namespace nql {

namespace {

bool nameHas(const std::string& name, const char* sub) {
  return name.find(sub) != std::string::npos;
}

const char* kPaths[] = {
    "/", "/index.html", "/login", "/logout", "/admin/panel",
    "/api/v1/users", "/api/v1/orders", "/api/v1/health", "/static/app.js",
};
const char* kUsers[] = {"alice", "bob", "carol", "dave", "eve", "mallory", "root"};
const char* kMsgs[] = {
    "connection established", "connection reset by peer", "auth failure for user",
    "slow query detected", "cache miss", "cache hit", "healthcheck ok",
};
const uint16_t kPorts[] = {80, 443, 443, 443, 22, 53, 53, 8080, 3306, 5432, 6379, 3389, 23, 4444};
const uint8_t kProtos[] = {6, 6, 6, 6, 17, 17, 1};
const uint8_t kFlags[] = {0x02, 0x12, 0x10, 0x10, 0x18, 0x11, 0x04, 0x18};

template <typename T, size_t N>
T pick(std::mt19937_64& rng, const T (&arr)[N]) {
  return arr[rng() % N];
}

uint32_t randomIp(std::mt19937_64& rng, bool preferPrivate) {
  uint32_t r = (uint32_t)rng();
  if (preferPrivate && (rng() % 100) < 60) {
    switch (rng() % 3) {
      case 0: return (10u << 24) | (r & 0x00FFFFFF);
      case 1: return (192u << 24) | (168u << 16) | (r & 0xFFFF);
      default: return (172u << 24) | ((16 + (r >> 24) % 16) << 16) | (r & 0xFFFF);
    }
  }
  if ((rng() % 100) < 12) return (185u << 24) | (r & 0x00FFFFFF);
  return r | 0x01000000;
}

} // namespace

RecordSet synthesize(const Schema& schema, size_t n, uint64_t seed) {
  std::mt19937_64 rng(seed);
  RecordSet rs(schema);
  rs.reserve(n);
  uint64_t ts = 1735689600ull * 1000;

  for (size_t i = 0; i < n; i++) {
    uint8_t* rec = rs.append();
    ts += rng() % 50;
    for (const auto& f : schema.fields) {
      const std::string& nm = f.name;
      switch (f.ty) {
        case Ty::IP4:
          storeUInt(rec, f, randomIp(rng, nameHas(nm, "src")));
          break;
        case Ty::Str: {
          const char* s;
          if (nameHas(nm, "path") || nameHas(nm, "url")) s = pick(rng, kPaths);
          else if (nameHas(nm, "user")) s = pick(rng, kUsers);
          else s = pick(rng, kMsgs);
          storeStr(rec, f, rs.arena().intern(s));
          break;
        }
        case Ty::F64:
          storeF64(rec, f, (double)(rng() % 1000) / 10.0);
          break;
        case Ty::Bool:
          storeUInt(rec, f, rng() % 2);
          break;
        default: {
          uint64_t v;
          if (nameHas(nm, "port")) v = (rng() % 3) ? pick(rng, kPorts) : 1024 + rng() % 64511;
          else if (nameHas(nm, "proto")) v = pick(rng, kProtos);
          else if (nameHas(nm, "flag")) v = pick(rng, kFlags);
          else if (nameHas(nm, "len")) v = 40 + rng() % 1460;
          else if (nameHas(nm, "ts")) v = ts;
          else v = rng() % 100000;
          if (f.size < 8) v &= (1ull << (f.size * 8)) - 1;
          storeUInt(rec, f, v);
          break;
        }
      }
    }
  }
  return rs;
}

} // namespace nql
