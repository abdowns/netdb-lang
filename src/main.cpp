#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>

#include "data.hpp"
#include "diag.hpp"
#include "interp.hpp"
#include "jit.hpp"
#include "parser.hpp"
#include "sema.hpp"

using namespace nql;

namespace {

struct Args {
  std::string command;
  std::string file;
  std::string filterName;
  size_t n = 20000;
  uint64_t seed = 42;
  bool noVerify = false;
};

[[noreturn]] void usage() {
  std::cerr <<
      "nql — a JIT-compiled query language for packets, logs and records\n"
      "\n"
      "usage:\n"
      "  nql run <file.nql> [options]   compile and execute filters\n"
      "\n"
      "run options:\n"
      "  --n <count>         synthesize this many records (default 20000)\n"
      "  --filter <name>     run one filter (default: all filters)\n"
      "  --seed <n>          synthetic data seed (default 42)\n"
      "  --no-verify         skip cross-checking JIT results against the interpreter\n";
  exit(2);
}

double msSince(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
      .count();
}

Program loadProgram(const std::string& path) {
  std::ifstream in(path);
  if (!in) fail("cannot open '" + path + "'");
  std::stringstream ss;
  ss << in.rdbuf();
  Program prog = parse(ss.str());
  analyze(prog);
  return prog;
}

void crossCheck(const FilterDecl& f, CompiledPredicate cp, const RecordSet& rs) {
  size_t k = std::min<size_t>(rs.count(), 1000);
  for (size_t i = 0; i < k; i++) {
    bool jit = cp.pred(rs.at(i));
    bool interp = evalFilter(f, rs.at(i));
    if (jit != interp)
      fail("JIT/interpreter mismatch in filter " + f.name + " on record " + std::to_string(i));
  }
  std::cerr << "  [verified filter " << f.name << ": JIT == interpreter on " << k
            << " records]\n";
}

void runFilter(Engine& engine, const FilterDecl& f, const Args& args) {
  RecordSet rs = synthesize(*f.schema, args.n, args.seed);
  std::cerr << "synthesized " << rs.count() << " " << f.schema->name << " records (seed "
            << args.seed << ")\n";
  CompiledPredicate cp = engine.filter(f.name);
  if (!args.noVerify) crossCheck(f, cp, rs);

  size_t n = rs.count();
  std::vector<uint64_t> idx(n);
  auto t0 = std::chrono::steady_clock::now();
  idx.resize(cp.collect(rs.data(), n, idx.data(), n));
  double ms = msSince(t0);

  std::cout << "== filter " << f.name << ": " << idx.size() << " of " << n
            << " records match (" << ms << " ms)\n";
  for (size_t i = 0; i < idx.size() && i < 10; i++) std::cout << "  #" << idx[i] << '\n';
  if (idx.size() > 10) std::cout << "  ...\n";
}

int cmdRun(const Args& args) {
  Program prog = loadProgram(args.file);
  if (prog.filters.empty()) fail("'" + args.file + "' defines no filters");

  Engine engine;
  auto t0 = std::chrono::steady_clock::now();
  engine.compile(prog);
  std::cerr << "JIT compiled " << prog.filters.size() << " filter(s) in " << msSince(t0)
            << " ms\n";

  if (!args.filterName.empty()) {
    FilterDecl* f = prog.findFilter(args.filterName);
    if (!f) fail("no filter named '" + args.filterName + "'");
    runFilter(engine, *f, args);
  } else {
    for (const auto& f : prog.filters) runFilter(engine, f, args);
  }
  return 0;
}

Args parseArgs(int argc, char** argv) {
  Args a;
  if (argc < 3) usage();
  a.command = argv[1];
  a.file = argv[2];
  for (int i = 3; i < argc; i++) {
    std::string arg = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) fail("missing value for " + arg);
      return argv[++i];
    };
    if (arg == "--filter") a.filterName = value();
    else if (arg == "--n") a.n = std::stoull(value());
    else if (arg == "--seed") a.seed = std::stoull(value());
    else if (arg == "--no-verify") a.noVerify = true;
    else fail("unknown option '" + arg + "'");
  }
  return a;
}

} // namespace

int main(int argc, char** argv) {
  try {
    Args args = parseArgs(argc, argv);
    if (args.command == "run") return cmdRun(args);
    usage();
  } catch (const DiagError& e) {
    std::cerr << "nql: " << e.what() << '\n';
    return 1;
  }
}
