// implements the c api in nql.h, thin glue over the existing engine
#include "nql.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ast.hpp"
#include "diag.hpp"
#include "interp.hpp"
#include "jit.hpp"
#include "parser.hpp"
#include "planner.hpp"
#include "reflect.hpp"
#include "sema.hpp"
#include "types.hpp"

namespace {

nql_type toCType(nql::Ty t) {
  switch (t) {
    case nql::Ty::Bool: return NQL_BOOL;
    case nql::Ty::U8: return NQL_U8;
    case nql::Ty::U16: return NQL_U16;
    case nql::Ty::U32: return NQL_U32;
    case nql::Ty::U64: return NQL_U64;
    case nql::Ty::I8: return NQL_I8;
    case nql::Ty::I16: return NQL_I16;
    case nql::Ty::I32: return NQL_I32;
    case nql::Ty::I64: return NQL_I64;
    case nql::Ty::F64: return NQL_F64;
    case nql::Ty::Str: return NQL_STR;
    case nql::Ty::IP4: return NQL_IP4;
    default: return NQL_INVALID;
  }
}

char* dupCString(const std::string& s) {
  char* out = static_cast<char*>(std::malloc(s.size() + 1));
  if (!out) return nullptr;
  std::memcpy(out, s.data(), s.size());
  out[s.size()] = '\0';
  return out;
}

// nql errors are prefixed with line and col from the combined buffer
// rewrite to be relative to user_src by subtracting the prelude line count
std::string adjustDiagForUserSrc(const std::string& msg, uint32_t preludeLines) {
  size_t c1 = msg.find(':');
  if (c1 == std::string::npos || c1 == 0) return msg;
  size_t c2 = msg.find(':', c1 + 1);
  if (c2 == std::string::npos) return msg;
  std::string linePart = msg.substr(0, c1);
  std::string colPart = msg.substr(c1 + 1, c2 - c1 - 1);
  auto allDigits = [](const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
  };
  if (!allDigits(linePart) || !allDigits(colPart)) return msg;

  long line = std::strtol(linePart.c_str(), nullptr, 10);
  std::string rest = msg.substr(c2); // error text after position
  if (line <= static_cast<long>(preludeLines))
    return "prelude:" + linePart + ":" + colPart + rest + " (bug in host-defined prelude)";
  return std::to_string(line - preludeLines) + ":" + colPart + rest;
}

} // namespace

struct nql_program {
  nql::Program prog;
  nql::Engine engine;
  std::vector<nql_schema> schemas;
  std::vector<std::vector<nql_field>> fields; // fields[i] backs schemas[i]

  explicit nql_program(nql::Program p) : prog(std::move(p)) {}
};

extern "C" {

nql_program* nql_compile(const char* prelude, const char* user_src, char** err_out) {
  if (err_out) *err_out = nullptr;
  std::string pre(prelude ? prelude : "");
  if (!pre.empty() && pre.back() != '\n') pre.push_back('\n');
  uint32_t preludeLines = static_cast<uint32_t>(std::count(pre.begin(), pre.end(), '\n'));
  std::string combined = pre + (user_src ? user_src : "");

  try {
    nql::Program parsed = nql::parse(combined);
    nql::analyze(parsed);
    nql::plan(parsed);

    auto out = std::make_unique<nql_program>(std::move(parsed));
    out->engine.compile(out->prog, /*optimize=*/true);

    size_t nschemas = out->prog.schemas.size();
    out->schemas.reserve(nschemas);
    out->fields.resize(nschemas);
    for (size_t i = 0; i < nschemas; i++) {
      const nql::Schema& s = *out->prog.schemas[i];
      auto& fv = out->fields[i];
      fv.reserve(s.fields.size());
      for (const auto& f : s.fields)
        fv.push_back(nql_field{f.name.c_str(), toCType(f.ty), f.offset, f.size, f.align});
      out->schemas.push_back(nql_schema{s.name.c_str(), s.size, s.align,
                                        static_cast<uint32_t>(fv.size()), fv.data()});
    }
    return out.release();
  } catch (const nql::DiagError& e) {
    if (err_out) *err_out = dupCString(adjustDiagForUserSrc(e.what(), preludeLines));
    return nullptr;
  } catch (const std::exception& e) {
    if (err_out) *err_out = dupCString(std::string("error: ") + e.what());
    return nullptr;
  }
}

void nql_free(nql_program* prog) { delete prog; }
void nql_free_string(char* s) { std::free(s); }

size_t nql_schema_count(const nql_program* prog) { return prog->schemas.size(); }

const nql_schema* nql_schema_at(const nql_program* prog, size_t idx) {
  return idx < prog->schemas.size() ? &prog->schemas[idx] : nullptr;
}

const nql_schema* nql_schema_by_name(const nql_program* prog, const char* name) {
  for (const auto& s : prog->schemas)
    if (std::strcmp(s.name, name) == 0) return &s;
  return nullptr;
}

const nql_field* nql_field_at(const nql_schema* schema, size_t idx) {
  return idx < schema->field_count ? &schema->fields[idx] : nullptr;
}

const nql_field* nql_field_by_name(const nql_schema* schema, const char* name) {
  for (uint32_t i = 0; i < schema->field_count; i++)
    if (std::strcmp(schema->fields[i].name, name) == 0) return &schema->fields[i];
  return nullptr;
}

size_t nql_filter_count(const nql_program* prog) { return prog->prog.filters.size(); }
const char* nql_filter_name_at(const nql_program* prog, size_t idx) {
  return idx < prog->prog.filters.size() ? prog->prog.filters[idx].name.c_str() : nullptr;
}
const char* nql_filter_schema_at(const nql_program* prog, size_t idx) {
  return idx < prog->prog.filters.size() ? prog->prog.filters[idx].schemaName.c_str() : nullptr;
}

size_t nql_query_count(const nql_program* prog) { return prog->prog.queries.size(); }
const char* nql_query_name_at(const nql_program* prog, size_t idx) {
  return idx < prog->prog.queries.size() ? prog->prog.queries[idx].name.c_str() : nullptr;
}
const char* nql_query_schema_at(const nql_program* prog, size_t idx) {
  return idx < prog->prog.queries.size() ? prog->prog.queries[idx].schemaName.c_str() : nullptr;
}
int64_t nql_query_limit_at(const nql_program* prog, size_t idx) {
  return idx < prog->prog.queries.size() ? prog->prog.queries[idx].limit : -1;
}

bool nql_filter_kernels(const nql_program* prog, const char* name, nql_kernels* out) {
  bool found = false;
  for (const auto& f : prog->prog.filters)
    if (f.name == name) { found = true; break; }
  if (!found) return false;
  // engine filter and query are not const in jit.hpp but only do a symbol
  // lookup, so const cast here keeps this api const correct for callers
  nql::CompiledPredicate cp = const_cast<nql::Engine&>(prog->engine).filter(name);
  out->pred = cp.pred;
  out->count = cp.count;
  out->collect = cp.collect;
  return true;
}

bool nql_query_kernels(const nql_program* prog, const char* name, nql_kernels* out) {
  const nql::QueryDecl* q = nullptr;
  for (const auto& qq : prog->prog.queries)
    if (qq.name == name) { q = &qq; break; }
  if (!q) return false;
  if (!q->where) {
    out->pred = nullptr;
    out->count = nullptr;
    out->collect = nullptr;
    return true;
  }
  nql::CompiledPredicate cp = const_cast<nql::Engine&>(prog->engine).query(name);
  out->pred = cp.pred;
  out->count = cp.count;
  out->collect = cp.collect;
  return true;
}

int nql_eval_filter_interp(const nql_program* prog, const char* name, const void* rec) {
  const nql::FilterDecl* f = nullptr;
  for (const auto& ff : prog->prog.filters)
    if (ff.name == name) { f = &ff; break; }
  if (!f) return -1;
  return nql::evalFilter(*f, static_cast<const uint8_t*>(rec)) ? 1 : 0;
}

} // extern "C"
