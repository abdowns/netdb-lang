#include "jit.hpp"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include "codegen.hpp"

namespace nql {

using namespace llvm;
using namespace llvm::orc;

static void optimizeModule(Module& M) {
  PassBuilder PB;
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
  ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2);
  MPM.run(M, MAM);
}

template <typename T>
static T orDie(Expected<T> e, const char* what) {
  if (!e) fail(std::string(what) + ": " + toString(e.takeError()));
  return std::move(*e);
}

Engine::Engine() {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  jit_ = orDie(LLJITBuilder().create(), "failed to create JIT");
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

  optimizeModule(*mod);

  if (auto err = jit_->addIRModule(ThreadSafeModule(std::move(mod), std::move(ctx))))
    fail("failed to add module to JIT: " + toString(std::move(err)));
}

void* Engine::lookup(const std::string& sym) {
  auto addr = orDie(jit_->lookup(sym), ("lookup of '" + sym + "' failed").c_str());
  return addr.toPtr<void*>();
}

CompiledPredicate Engine::predicate(const std::string& sym) {
  CompiledPredicate p;
  p.pred = reinterpret_cast<CompiledPredicate::PredFn>(lookup(sym));
  p.count = reinterpret_cast<CompiledPredicate::CountFn>(lookup(sym + "$count"));
  p.collect = reinterpret_cast<CompiledPredicate::CollectFn>(lookup(sym + "$collect"));
  return p;
}

} // namespace nql
