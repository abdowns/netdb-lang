#include "data.hpp"

#include <charconv>
#include <fstream>
#include <random>

#include "diag.hpp"

namespace nql {

static bool parseIp(std::string_view s, uint32_t& out) {
  uint32_t octets[4];
  size_t pos = 0;
  for (int i = 0; i < 4; i++) {
    if (i > 0) {
      if (pos >= s.size() || s[pos] != '.') return false;
      pos++;
    }
    uint32_t v = 0;
    size_t digits = 0;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9' && digits < 4) {
      v = v * 10 + (s[pos] - '0');
      pos++;
      digits++;
    }
    if (digits == 0 || digits > 3 || v > 255) return false;
    octets[i] = v;
  }
  if (pos != s.size()) return false;
  out = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
  return true;
}

void parseFieldValue(uint8_t* rec, const FieldInfo& f, std::string_view text, Arena& arena,
                     const std::string& context) {
  switch (f.ty) {
    case Ty::Bool: {
      if (text == "true" || text == "1") storeUInt(rec, f, 1);
      else if (text == "false" || text == "0") storeUInt(rec, f, 0);
      else fail(context + ": bad bool '" + std::string(text) + "'");
      return;
    }
    case Ty::F64: {
      double v;
      auto [p, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
      if (ec != std::errc() || p != text.data() + text.size())
        fail(context + ": bad float '" + std::string(text) + "'");
      storeF64(rec, f, v);
      return;
    }
    case Ty::Str:
      storeStr(rec, f, arena.intern(text));
      return;
    case Ty::IP4: {
      uint32_t ip;
      if (!parseIp(text, ip)) fail(context + ": bad IPv4 address '" + std::string(text) + "'");
      storeUInt(rec, f, ip);
      return;
    }
    default: {
      if (isUnsignedTy(f.ty)) {
        uint64_t v;
        auto [p, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
        if (ec != std::errc() || p != text.data() + text.size())
          fail(context + ": bad integer '" + std::string(text) + "'");
        storeUInt(rec, f, v);
      } else {
        int64_t v;
        auto [p, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
        if (ec != std::errc() || p != text.data() + text.size())
          fail(context + ": bad integer '" + std::string(text) + "'");
        storeUInt(rec, f, (uint64_t)v);
      }
      return;
    }
  }
}

static std::vector<std::string> splitCsvLine(const std::string& line, size_t lineNo) {
  std::vector<std::string> out;
  std::string cur;
  bool inQuotes = false;
  for (size_t i = 0; i < line.size(); i++) {
    char c = line[i];
    if (inQuotes) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          cur += '"';
          i++;
        } else {
          inQuotes = false;
        }
      } else {
        cur += c;
      }
    } else if (c == '"') {
      inQuotes = true;
    } else if (c == ',') {
      out.push_back(std::move(cur));
      cur.clear();
    } else if (c == '\r') {
    } else {
      cur += c;
    }
  }
  if (inQuotes) fail("CSV line " + std::to_string(lineNo) + ": unterminated quote");
  out.push_back(std::move(cur));
  return out;
}

RecordSet loadCsv(const Schema& schema, const std::string& path) {
  std::ifstream in(path);
  if (!in) fail("cannot open '" + path + "'");

  std::string line;
  if (!std::getline(in, line)) fail("'" + path + "' is empty (need a header row)");
  std::vector<std::string> header = splitCsvLine(line, 1);

  std::vector<int> fieldCol(schema.fields.size(), -1);
  for (size_t f = 0; f < schema.fields.size(); f++) {
    for (size_t c = 0; c < header.size(); c++) {
      if (header[c] == schema.fields[f].name) {
        fieldCol[f] = (int)c;
        break;
      }
    }
    if (fieldCol[f] < 0)
      fail("'" + path + "': CSV header has no column for field '" + schema.fields[f].name +
           "' of schema " + schema.name);
  }

  RecordSet rs(schema);
  size_t lineNo = 1;
  while (std::getline(in, line)) {
    lineNo++;
    if (line.empty() || (line.size() == 1 && line[0] == '\r')) continue;
    std::vector<std::string> cols = splitCsvLine(line, lineNo);
    uint8_t* rec = rs.append();
    for (size_t f = 0; f < schema.fields.size(); f++) {
      int c = fieldCol[f];
      if ((size_t)c >= cols.size())
        fail(path + " line " + std::to_string(lineNo) + ": too few columns");
      parseFieldValue(rec, schema.fields[f], cols[c], rs.arena(),
                      path + " line " + std::to_string(lineNo) + ", field '" +
                          schema.fields[f].name + "'");
    }
  }
  return rs;
}

namespace {

bool nameHas(const std::string& name, const char* sub) {
  return name.find(sub) != std::string::npos;
}

const char* kPaths[] = {
    "/", "/index.html", "/login", "/logout", "/admin/panel", "/admin/users",
    "/api/v1/users", "/api/v1/orders", "/api/v1/health", "/static/app.js",
    "/static/style.css", "/images/logo.png", "/search?q=llvm", "/wp-login.php",
    "/checkout", "/cart", "/api/v2/metrics", "/.env", "/robots.txt", "/feed.xml",
};
const char* kMethods[] = {"GET", "GET", "GET", "GET", "POST", "POST", "PUT", "DELETE", "HEAD"};
const char* kUsers[] = {"alice", "bob", "carol", "dave", "eve", "mallory", "trent", "peggy", "root", "svc_backup"};
const char* kHosts[] = {"web-01", "web-02", "api-01", "api-02", "db-01", "cache-01", "lb-01", "worker-03"};
const char* kAgents[] = {"curl/8.4", "Mozilla/5.0", "Go-http-client/2.0", "python-requests/2.31", "kube-probe/1.29", "sqlmap/1.7"};
const char* kMsgs[] = {
    "connection established", "connection reset by peer", "auth failure for user",
    "slow query detected", "cache miss", "cache hit", "TLS handshake failed",
    "rate limit exceeded", "disk usage above threshold", "healthcheck ok",
};
const uint16_t kPorts[] = {80, 443, 443, 443, 22, 53, 53, 8080, 3306, 5432, 6379, 3389, 23, 4444};
const uint16_t kStatus[] = {200, 200, 200, 200, 200, 200, 301, 302, 304, 400, 401, 403, 404, 404, 500, 502};
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

  auto internPool = [&](auto& pool) {
    std::vector<StrRef> out;
    for (const char* s : pool) out.push_back(rs.arena().intern(s));
    return out;
  };
  auto paths = internPool(kPaths);
  auto methods = internPool(kMethods);
  auto users = internPool(kUsers);
  auto hosts = internPool(kHosts);
  auto agents = internPool(kAgents);
  auto msgs = internPool(kMsgs);

  for (size_t i = 0; i < n; i++) {
    uint8_t* rec = rs.append();
    ts += rng() % 50;
    for (const auto& f : schema.fields) {
      const std::string& nm = f.name;
      switch (f.ty) {
        case Ty::IP4:
          storeUInt(rec, f, randomIp(rng, nameHas(nm, "src") || nameHas(nm, "client")));
          break;
        case Ty::Str: {
          const std::vector<StrRef>* pool = &msgs;
          if (nameHas(nm, "path") || nameHas(nm, "url") || nameHas(nm, "uri")) pool = &paths;
          else if (nameHas(nm, "method")) pool = &methods;
          else if (nameHas(nm, "user") || nameHas(nm, "name")) pool = &users;
          else if (nameHas(nm, "host") || nameHas(nm, "domain")) pool = &hosts;
          else if (nameHas(nm, "agent")) pool = &agents;
          storeStr(rec, f, (*pool)[rng() % pool->size()]);
          break;
        }
        case Ty::F64: {
          double v = (double)(rng() % 1000) / 10.0;
          if (rng() % 20 == 0) v *= 50;
          storeF64(rec, f, v);
          break;
        }
        case Ty::Bool:
          storeUInt(rec, f, rng() % 2);
          break;
        default: {
          uint64_t v;
          if (nameHas(nm, "port")) v = (rng() % 3) ? pick(rng, kPorts) : 1024 + rng() % 64511;
          else if (nameHas(nm, "proto")) v = pick(rng, kProtos);
          else if (nameHas(nm, "status") || nameHas(nm, "code")) v = pick(rng, kStatus);
          else if (nameHas(nm, "flag")) v = pick(rng, kFlags);
          else if (nameHas(nm, "len") || nameHas(nm, "size") || nameHas(nm, "bytes")) {
            v = 40 + rng() % 200;
            if (rng() % 10 == 0) v = 1000 + rng() % 64535;
          } else if (nameHas(nm, "ts") || nameHas(nm, "time")) {
            v = ts;
          } else {
            v = rng() % 100000;
          }
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
