#pragma once
#include <memory>
#include <string>

#include "ast.hpp"

namespace llvm::orc {
class LLJIT;
}

namespace nql {

using PredFn = bool (*)(const void* record);

class Engine {
public:
  Engine();
  ~Engine();

  void compile(const Program& prog);

  PredFn filter(const std::string& name);

private:
  std::unique_ptr<llvm::orc::LLJIT> jit_;
};

} // namespace nql
