#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>

#include "data.hpp"
#include "diag.hpp"
#include "interp.hpp"
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
};

[[noreturn]] void usage() {
  std::cerr <<
      "nql — a query language for packets, logs and records\n"
      "\n"
      "usage:\n"
      "  nql run <file.nql> [options]   evaluate filters over synthetic records\n"
      "\n"
      "run options:\n"
      "  --n <count>         synthesize this many records (default 20000)\n"
      "  --filter <name>     run one filter (default: all filters)\n"
      "  --seed <n>          synthetic data seed (default 42)\n";
  exit(2);
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

void runFilter(const FilterDecl& f, const Args& args) {
  RecordSet rs = synthesize(*f.schema, args.n, args.seed);
  std::cerr << "synthesized " << rs.count() << " " << f.schema->name << " records (seed "
            << args.seed << ")\n";

  std::vector<size_t> matches;
  auto t0 = std::chrono::steady_clock::now();
  for (size_t i = 0; i < rs.count(); i++)
    if (evalFilter(f, rs.at(i))) matches.push_back(i);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                  .count();

  std::cout << "== filter " << f.name << ": " << matches.size() << " of " << rs.count()
            << " records match (" << ms << " ms)\n";
  for (size_t i = 0; i < matches.size() && i < 10; i++) std::cout << "  #" << matches[i] << '\n';
  if (matches.size() > 10) std::cout << "  ...\n";
}

int cmdRun(const Args& args) {
  Program prog = loadProgram(args.file);
  if (prog.filters.empty()) fail("'" + args.file + "' defines no filters");

  if (!args.filterName.empty()) {
    FilterDecl* f = prog.findFilter(args.filterName);
    if (!f) fail("no filter named '" + args.filterName + "'");
    runFilter(*f, args);
  } else {
    for (const auto& f : prog.filters) runFilter(f, args);
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
