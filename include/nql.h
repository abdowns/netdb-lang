// c api over the nql engine, for embedding nql in another language
// compiles a prelude plus user source as one program so errors report
// positions relative to user source only
//
// nql_program is immutable and safe for concurrent use after compile
// nql_free must not race with in flight kernel calls on the same program
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nql_program nql_program;

// mirrors nql::Ty
typedef enum nql_type {
  NQL_INVALID = 0,
  NQL_BOOL,
  NQL_U8, NQL_U16, NQL_U32, NQL_U64,
  NQL_I8, NQL_I16, NQL_I32, NQL_I64,
  NQL_F64,
  NQL_STR,
  NQL_IP4,
} nql_type;

typedef struct nql_field {
  const char* name;   // borrowed, lives as long as prog
  nql_type ty;
  uint32_t offset;
  uint32_t size;
  uint32_t align;
} nql_field;

// record layout, name, size, align, fixed field list
// pointers borrowed from prog, valid for its lifetime
typedef struct nql_schema {
  const char* name;
  uint32_t size;
  uint32_t align;
  uint32_t field_count;
  const nql_field* fields; // field_count entries, declaration order
} nql_schema;

// jit compiled kernels for one filter or query
// collect is null when a query has no where clause, meaning it matches everything
typedef struct nql_kernels {
  // true if rec matches
  bool (*pred)(const void* rec);
  // count of matches among first n records at base
  uint64_t (*count)(const void* base, uint64_t n);
  // writes up to cap matching indices to out_idx in scan order
  // returns how many were written, may stop early once cap is hit
  uint64_t (*collect)(const void* base, uint64_t n, uint64_t* out_idx, uint64_t cap);
} nql_kernels;

// compiles prelude plus user_src into one program
// on failure returns null and sets err_out to a malloc'd message,
// free it with nql_free_string, positions are relative to user_src
nql_program* nql_compile(const char* prelude, const char* user_src, char** err_out);

void nql_free(nql_program* prog);

// frees a string returned by this api
void nql_free_string(char* s);

// introspection

size_t nql_schema_count(const nql_program* prog);
// null if idx out of range
const nql_schema* nql_schema_at(const nql_program* prog, size_t idx);
const nql_schema* nql_schema_by_name(const nql_program* prog, const char* name);
// wraps schema fields, null if not found
const nql_field* nql_field_at(const nql_schema* schema, size_t idx);
const nql_field* nql_field_by_name(const nql_schema* schema, const char* name);

size_t nql_filter_count(const nql_program* prog);
const char* nql_filter_name_at(const nql_program* prog, size_t idx);
const char* nql_filter_schema_at(const nql_program* prog, size_t idx);

size_t nql_query_count(const nql_program* prog);
const char* nql_query_name_at(const nql_program* prog, size_t idx);
const char* nql_query_schema_at(const nql_program* prog, size_t idx);
// -1 if no limit clause
int64_t nql_query_limit_at(const nql_program* prog, size_t idx);

// kernels

// looks up kernels by name, false if not found
bool nql_filter_kernels(const nql_program* prog, const char* name, nql_kernels* out);
bool nql_query_kernels(const nql_program* prog, const char* name, nql_kernels* out);

// evaluates a filter with the interpreter instead of jit, for cross checking
// returns 0 or 1, or -1 if not found
int nql_eval_filter_interp(const nql_program* prog, const char* name, const void* rec);

#ifdef __cplusplus
}
#endif
