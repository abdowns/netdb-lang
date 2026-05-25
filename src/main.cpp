#include <cstdint>
#include <iostream>
#include <string>

#include "diag.hpp"
#include "types.hpp"

using namespace nql;

namespace {

[[noreturn]] void usage() {
  std::cerr <<
      "nql — a query language for packets, logs and records\n"
      "\n"
      "usage:\n"
      "  nql types            list the built-in field types\n";
  exit(2);
}

int cmdTypes() {
  static const Ty kAll[] = {Ty::Bool, Ty::U8,  Ty::U16, Ty::U32, Ty::U64, Ty::I8,
                            Ty::I16,  Ty::I32, Ty::I64, Ty::F64, Ty::Str, Ty::IP4};
  for (Ty t : kAll) {
    char buf[64];
    snprintf(buf, sizeof buf, "  %-5s size=%-2u align=%u%s\n", tyName(t), tySize(t),
             tyAlign(t), isUnsignedTy(t) ? "  unsigned" : "");
    std::cout << buf;
  }
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) usage();
    std::string command = argv[1];
    if (command == "types") return cmdTypes();
    fail("unknown command '" + command + "'");
  } catch (const DiagError& e) {
    std::cerr << "nql: " << e.what() << '\n';
    return 1;
  }
}
