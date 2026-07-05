#pragma once
#include <memory>
#include <string>

#include "ast.hpp"

namespace llvm::orc {
class LLJIT;
}

namespace nql {

struct CompiledPredicate {
  using PredFn = bool (*)(const void* record);
  using CountFn = uint64_t (*)(const void* base, uint64_t n);
  using CollectFn = uint64_t (*)(const void* base, uint64_t n, uint64_t* outIdx, uint64_t cap);

  PredFn pred = nullptr;
  CountFn count = nullptr;
  CollectFn collect = nullptr;
};

class Engine {
public:
  Engine();
  ~Engine();

  // returns final IR text, for `dump --ir`
  std::string compile(const Program& prog, bool optimize = true);

  CompiledPredicate filter(const std::string& name) { return predicate("f$" + name); }
  CompiledPredicate query(const std::string& name) { return predicate("q$" + name); }

private:
  CompiledPredicate predicate(const std::string& sym);
  void* lookup(const std::string& sym);

  std::unique_ptr<llvm::orc::LLJIT> jit_;
};

} // namespace nql
