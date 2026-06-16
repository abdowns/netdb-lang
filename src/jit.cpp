#include "jit.hpp"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include "codegen.hpp"

namespace nql {

using namespace llvm;
using namespace llvm::orc;

Engine::Engine() {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  auto jit = LLJITBuilder().create();
  if (!jit) fail("failed to create JIT: " + toString(jit.takeError()));
  jit_ = std::move(*jit);
}

Engine::~Engine() = default;

void Engine::compile(const Program& prog) {
  auto ctx = std::make_unique<LLVMContext>();
  auto mod = std::make_unique<Module>("nql_jit", *ctx);
  mod->setDataLayout(jit_->getDataLayout());
#if LLVM_VERSION_MAJOR >= 21
  mod->setTargetTriple(jit_->getTargetTriple());
#else
  mod->setTargetTriple(jit_->getTargetTriple().str());
#endif

  emitProgram(prog, *mod);

  if (verifyModule(*mod, &errs()))
    fail("internal error: generated invalid IR");

  if (auto err = jit_->addIRModule(ThreadSafeModule(std::move(mod), std::move(ctx))))
    fail("failed to add module to JIT: " + toString(std::move(err)));
}

Engine::PredFn Engine::lookup(const std::string& sym) {
  auto addr = jit_->lookup(sym);
  if (!addr) fail("lookup of '" + sym + "' failed: " + toString(addr.takeError()));
  return addr->toPtr<PredFn>();
}

} // namespace nql
