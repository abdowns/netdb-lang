#pragma once
#include "ast.hpp"

namespace llvm {
class Module;
}

namespace nql {

// symbols per predicate: f$name/q$name, plus $count and $collect batch kernels
void emitProgram(const Program& prog, llvm::Module& M);

} // namespace nql
