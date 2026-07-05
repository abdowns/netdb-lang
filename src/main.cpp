#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include "data.hpp"
#include "diag.hpp"
#include "interp.hpp"
#include "jit.hpp"
#include "output.hpp"
#include "parser.hpp"
#include "planner.hpp"
#include "sema.hpp"

using namespace nql;

namespace {

struct Args {
  std::string command;
  std::string file;
  std::string dataPath;
  std::string filterName;
  std::string queryName;
  OutFormat out = OutFormat::Table;
  int64_t limit = -1;
  size_t n = 20000;
  uint64_t seed = 42;
  bool noOpt = false;
  bool noVerify = false;
  bool dumpAst = false, dumpReflect = false, dumpPlan = false, dumpIr = false;
};

[[noreturn]] void usage() {
  std::cerr <<
      "nql — a JIT-compiled query language for packets, logs and records\n"
      "\n"
      "usage:\n"
      "  nql run  <file.nql> [options]   compile and execute queries/filters\n"
      "  nql dump <file.nql> [options]   show reflection, AST, plan and LLVM IR\n"
      "\n"
      "run options:\n"
      "  --data <file.csv>   load records from CSV (columns matched to schema by name)\n"
      "  --n <count>         synthesize this many records when no --data (default 20000)\n"
      "  --filter <name>     run one filter (default: all queries, else all filters)\n"
      "  --query <name>      run one query\n"
      "  --out json|csv|table  output format (default table)\n"
      "  --limit <count>     cap the number of rows printed\n"
      "  --seed <n>          synthetic data seed (default 42)\n"
      "  --no-verify         skip cross-checking JIT results against the interpreter\n"
      "\n"
      "dump options:\n"
      "  --reflect --ast --plan --ir     pick sections (default: all)\n"
      "  --no-opt                        show unoptimized IR\n";
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
  plan(prog);
  return prog;
}

void crossCheck(const char* what, const Expr* body, const std::vector<LetStmt>& lets,
                CompiledPredicate cp, const RecordSet& rs) {
  size_t k = std::min<size_t>(rs.count(), 1000);
  for (size_t i = 0; i < k; i++) {
    bool jit = cp.pred(rs.at(i));
    bool interp = evalPredicate(body, lets, rs.at(i));
    if (jit != interp)
      fail(std::string("JIT/interpreter mismatch in ") + what + " on record " +
           std::to_string(i));
  }
  std::cerr << "  [verified " << what << ": JIT == interpreter on " << k << " records]\n";
}

void sortByField(std::vector<uint64_t>& idx, const RecordSet& rs, const FieldInfo& f,
                 bool desc) {
  auto less = [&](uint64_t a, uint64_t b) {
    const uint8_t *ra = rs.at(a), *rb = rs.at(b);
    int c;
    switch (f.ty) {
      case Ty::F64: {
        double x = loadF64(ra, f), y = loadF64(rb, f);
        c = x < y ? -1 : x > y ? 1 : 0;
        break;
      }
      case Ty::Str: {
        StrRef x = loadStr(ra, f), y = loadStr(rb, f);
        int m = std::memcmp(x.ptr, y.ptr, std::min(x.len, y.len));
        c = m ? m : (x.len < y.len ? -1 : x.len > y.len ? 1 : 0);
        break;
      }
      default:
        if (isUnsignedTy(f.ty) || f.ty == Ty::IP4 || f.ty == Ty::Bool) {
          uint64_t x = loadUInt(ra, f), y = loadUInt(rb, f);
          c = x < y ? -1 : x > y ? 1 : 0;
        } else {
          int64_t x = loadSInt(ra, f), y = loadSInt(rb, f);
          c = x < y ? -1 : x > y ? 1 : 0;
        }
    }
    return desc ? c > 0 : c < 0;
  };
  std::stable_sort(idx.begin(), idx.end(), less);
}

class DataSource {
public:
  DataSource(const Args& args) : args_(args) {}

  RecordSet& get(const Schema& schema) {
    auto it = sets_.find(schema.name);
    if (it != sets_.end()) return *it->second;
    auto t0 = std::chrono::steady_clock::now();
    std::unique_ptr<RecordSet> rs;
    if (!args_.dataPath.empty()) {
      rs = std::make_unique<RecordSet>(loadCsv(schema, args_.dataPath));
      std::cerr << "loaded " << rs->count() << " " << schema.name << " records from "
                << args_.dataPath << " (" << msSince(t0) << " ms)\n";
    } else {
      rs = std::make_unique<RecordSet>(synthesize(schema, args_.n, args_.seed));
      std::cerr << "synthesized " << rs->count() << " " << schema.name << " records (seed "
                << args_.seed << ")\n";
    }
    return *sets_.emplace(schema.name, std::move(rs)).first->second;
  }

private:
  const Args& args_;
  std::map<std::string, std::unique_ptr<RecordSet>> sets_;
};

void execute(const std::string& title, const RecordSet& rs, CompiledPredicate cp,
             const std::vector<const FieldInfo*>& select, const FieldInfo* orderBy,
             bool orderDesc, int64_t limit, OutFormat fmt) {
  size_t n = rs.count();
  std::vector<uint64_t> idx;
  double queryMs;

  if (cp.collect) {
    // an order by needs all matches before sorting; else the kernel stops at limit
    uint64_t cap = (orderBy || limit < 0) ? n : std::min<uint64_t>(limit, n);
    idx.resize(cap);
    auto t0 = std::chrono::steady_clock::now();
    uint64_t cnt = cp.collect(rs.data(), n, idx.data(), cap);
    queryMs = msSince(t0);
    idx.resize(cnt);
  } else {
    auto t0 = std::chrono::steady_clock::now();
    size_t take = (orderBy || limit < 0) ? n : std::min<size_t>(limit, n);
    idx.resize(take);
    for (size_t i = 0; i < take; i++) idx[i] = i;
    queryMs = msSince(t0);
  }

  if (orderBy) sortByField(idx, rs, *orderBy, orderDesc);
  if (limit >= 0 && idx.size() > (size_t)limit) idx.resize(limit);

  std::cerr << "== " << title << ": " << idx.size() << " rows (scanned " << n << " in "
            << queryMs << " ms)\n";
  writeRecords(std::cout, rs, idx, select, fmt);
  std::cout.flush();
}

int cmdRun(const Args& args) {
  Program prog = loadProgram(args.file);
  if (prog.filters.empty() && prog.queries.empty())
    fail("'" + args.file + "' defines no filters or queries");

  Engine engine;
  auto t0 = std::chrono::steady_clock::now();
  engine.compile(prog, !args.noOpt);
  std::cerr << "JIT compiled " << prog.filters.size() << " filter(s) and "
            << prog.queries.size() << " query(ies) in " << msSince(t0) << " ms\n";

  DataSource data(args);
  static const std::vector<const FieldInfo*> kAllFields;
  static const std::vector<LetStmt> kNoLets;

  auto runFilter = [&](const FilterDecl& f) {
    RecordSet& rs = data.get(*f.schema);
    CompiledPredicate cp = engine.filter(f.name);
    if (!args.noVerify) crossCheck(("filter " + f.name).c_str(), f.body.get(), f.lets, cp, rs);
    execute("filter " + f.name, rs, cp, kAllFields, nullptr, false, args.limit, args.out);
  };
  auto runQuery = [&](const QueryDecl& q) {
    RecordSet& rs = data.get(*q.schema);
    CompiledPredicate cp; // null when there is no where clause
    if (q.where) {
      cp = engine.query(q.name);
      if (!args.noVerify)
        crossCheck(("query " + q.name).c_str(), q.where.get(), kNoLets, cp, rs);
    }
    int64_t limit = args.limit >= 0 ? std::min<int64_t>(args.limit < 0 ? INT64_MAX : args.limit,
                                                        q.limit < 0 ? INT64_MAX : q.limit)
                                    : q.limit;
    execute("query " + q.name, rs, cp, q.selectInfo, q.orderInfo, q.orderDesc, limit, args.out);
  };

  if (!args.filterName.empty()) {
    FilterDecl* f = prog.findFilter(args.filterName);
    if (!f) fail("no filter named '" + args.filterName + "'");
    runFilter(*f);
  } else if (!args.queryName.empty()) {
    QueryDecl* q = prog.findQuery(args.queryName);
    if (!q) fail("no query named '" + args.queryName + "'");
    runQuery(*q);
  } else if (!prog.queries.empty()) {
    for (const auto& q : prog.queries) runQuery(q);
  } else {
    for (const auto& f : prog.filters) runFilter(f);
  }
  return 0;
}

int cmdDump(const Args& args) {
  Program prog = loadProgram(args.file);
  bool all = !args.dumpAst && !args.dumpReflect && !args.dumpPlan && !args.dumpIr;

  if (all || args.dumpReflect) {
    std::cout << "=== reflection ===\n";
    for (const auto& s : prog.schemas) {
      std::cout << "schema " << s->name << "  (size " << s->size << ", align " << s->align
                << ")\n";
      for (const auto& f : s->fields) {
        char buf[96];
        snprintf(buf, sizeof buf, "  %-12s %-5s offset=%-3u size=%-2u align=%u\n",
                 f.name.c_str(), tyName(f.ty), f.offset, f.size, f.align);
        std::cout << buf;
      }
    }
    std::cout << '\n';
  }

  if (all || args.dumpAst) {
    std::cout << "=== ast ===\n";
    for (const auto& f : prog.filters) {
      std::cout << "filter " << f.name << "(" << f.paramName << ": " << f.schemaName << ")\n";
      for (const auto& let : f.lets)
        std::cout << "  let " << let.name << ": " << tyName(let.type) << " = "
                  << exprToString(let.init.get()) << '\n';
      std::cout << "  " << exprToString(f.body.get()) << '\n';
    }
    for (const auto& q : prog.queries) {
      std::cout << "query " << q.name << " over " << q.schemaName << '\n';
      if (q.where) std::cout << "  where  " << exprToString(q.where.get()) << '\n';
      if (!q.selectFields.empty()) {
        std::cout << "  select ";
        for (size_t i = 0; i < q.selectFields.size(); i++)
          std::cout << (i ? ", " : "") << q.selectFields[i];
        std::cout << '\n';
      }
      if (!q.orderField.empty())
        std::cout << "  order by " << q.orderField << (q.orderDesc ? " desc" : " asc") << '\n';
      if (q.limit >= 0) std::cout << "  limit " << q.limit << '\n';
    }
    std::cout << '\n';
  }

  if (all || args.dumpPlan) {
    std::cout << "=== plan ===\n" << explainPlan(prog) << '\n';
  }

  if (all || args.dumpIr) {
    Engine engine;
    std::string ir = engine.compile(prog, !args.noOpt);
    std::cout << "=== llvm ir (" << (args.noOpt ? "unoptimized" : "O2") << ") ===\n"
              << ir << '\n';
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
    if (arg == "--data") a.dataPath = value();
    else if (arg == "--filter") a.filterName = value();
    else if (arg == "--query") a.queryName = value();
    else if (arg == "--limit") a.limit = std::stoll(value());
    else if (arg == "--n") a.n = std::stoull(value());
    else if (arg == "--seed") a.seed = std::stoull(value());
    else if (arg == "--out") {
      std::string v = value();
      if (v == "json") a.out = OutFormat::Json;
      else if (v == "csv") a.out = OutFormat::Csv;
      else if (v == "table") a.out = OutFormat::Table;
      else fail("bad --out format '" + v + "'");
    }
    else if (arg == "--no-opt") a.noOpt = true;
    else if (arg == "--no-verify") a.noVerify = true;
    else if (arg == "--ast") a.dumpAst = true;
    else if (arg == "--reflect") a.dumpReflect = true;
    else if (arg == "--plan") a.dumpPlan = true;
    else if (arg == "--ir") a.dumpIr = true;
    else fail("unknown option '" + arg + "'");
  }
  return a;
}

} // namespace

int main(int argc, char** argv) {
  try {
    Args args = parseArgs(argc, argv);
    if (args.command == "run") return cmdRun(args);
    if (args.command == "dump") return cmdDump(args);
    usage();
  } catch (const DiagError& e) {
    std::cerr << "nql: " << e.what() << '\n';
    return 1;
  }
}
