#include "rt.hpp"

#include <cstring>

extern "C" {

int32_t nql_str_eq(const char* a, uint64_t alen, const char* b, uint64_t blen) {
  return alen == blen && std::memcmp(a, b, alen) == 0;
}

int32_t nql_str_contains(const char* s, uint64_t slen, const char* sub, uint64_t sublen) {
  if (sublen == 0) return 1;
  if (sublen > slen) return 0;
  return memmem(s, slen, sub, sublen) != nullptr;
}

int32_t nql_str_starts(const char* s, uint64_t slen, const char* pre, uint64_t prelen) {
  return prelen <= slen && std::memcmp(s, pre, prelen) == 0;
}

int32_t nql_str_ends(const char* s, uint64_t slen, const char* suf, uint64_t suflen) {
  return suflen <= slen && std::memcmp(s + (slen - suflen), suf, suflen) == 0;
}

int32_t nql_str_glob(const char* s, uint64_t slen, const char* pat, uint64_t patlen) {
  // iterative glob with backtracking to the last '*'
  uint64_t si = 0, pi = 0;
  uint64_t starPi = UINT64_MAX, starSi = 0;
  while (si < slen) {
    if (pi < patlen && (pat[pi] == '?' || pat[pi] == s[si])) {
      si++;
      pi++;
    } else if (pi < patlen && pat[pi] == '*') {
      starPi = pi++;
      starSi = si;
    } else if (starPi != UINT64_MAX) {
      pi = starPi + 1;
      si = ++starSi;
    } else {
      return 0;
    }
  }
  while (pi < patlen && pat[pi] == '*') pi++;
  return pi == patlen;
}

} // extern "C"
