#pragma once
#include <cstdint>

// int32_t return (not bool) keeps the abi trivial for jit generated calls
extern "C" {

int32_t nql_str_eq(const char* a, uint64_t alen, const char* b, uint64_t blen);
int32_t nql_str_contains(const char* s, uint64_t slen, const char* sub, uint64_t sublen);
int32_t nql_str_starts(const char* s, uint64_t slen, const char* pre, uint64_t prelen);
int32_t nql_str_ends(const char* s, uint64_t slen, const char* suf, uint64_t suflen);
int32_t nql_str_glob(const char* s, uint64_t slen, const char* pat, uint64_t patlen);

} // extern "C"
