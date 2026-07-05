#include "jit.hpp"

#include <mutex>

#include "llvm/ExecutionEngine/Orc/AbsoluteSymbols.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include "codegen.hpp"
#include "rt.hpp"

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
  static std::once_flag once;
  std::call_once(once, [] {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
  });

  jit_ = orDie(LLJITBuilder().create(), "failed to create JIT");

  SymbolMap syms;
  auto addSym = [&](const char* name, auto* fn) {
    syms[jit_->mangleAndIntern(name)] = ExecutorSymbolDef(
        ExecutorAddr::fromPtr(fn), JITSymbolFlags::Exported | JITSymbolFlags::Callable);
  };
  addSym("nql_str_eq", &nql_str_eq);
  addSym("nql_str_contains", &nql_str_contains);
  addSym("nql_str_starts", &nql_str_starts);
  addSym("nql_str_ends", &nql_str_ends);
  addSym("nql_str_glob", &nql_str_glob);
  if (auto err = jit_->getMainJITDylib().define(absoluteSymbols(std::move(syms))))
    fail("failed to register runtime symbols: " + toString(std::move(err)));
}

Engine::~Engine() = default;

std::string Engine::compile(const Program& prog, bool optimize) {
  auto ctx = std::make_unique<LLVMContext>();
  auto mod = std::make_unique<Module>("nql_jit", *ctx);
  mod->setDataLayout(jit_->getDataLayout());
#if LLVM_VERSION_MAJOR >= 21
  mod->setTargetTriple(jit_->getTargetTriple());
#else
  mod->setTargetTriple(jit_->getTargetTriple().str());
#endif

  emitProgram(prog, *mod);

  std::string verifyErrs;
  raw_string_ostream verifyOS(verifyErrs);
  if (verifyModule(*mod, &verifyOS))
    fail("internal error: generated invalid IR:\n" + verifyErrs);

  if (optimize) optimizeModule(*mod);

  std::string ir;
  raw_string_ostream os(ir);
  mod->print(os, nullptr);

  if (auto err = jit_->addIRModule(ThreadSafeModule(std::move(mod), std::move(ctx))))
    fail("failed to add module to JIT: " + toString(std::move(err)));
  return ir;
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
