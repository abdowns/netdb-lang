#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace nql {

struct SrcLoc {
  uint32_t line = 0, col = 0;
};

class DiagError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

[[noreturn]] inline void fail(SrcLoc loc, const std::string& msg) {
  throw DiagError(std::to_string(loc.line) + ":" + std::to_string(loc.col) +
                  ": error: " + msg);
}

[[noreturn]] inline void fail(const std::string& msg) {
  throw DiagError("error: " + msg);
}

} // namespace nql
