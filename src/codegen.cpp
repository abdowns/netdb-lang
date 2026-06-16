#include "codegen.hpp"

#include <map>

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ModRef.h"

namespace nql {
namespace {

using namespace llvm;

struct CGVal {
  Value* v = nullptr;
  Value* len = nullptr;
};

class CodeGen {
public:
  CodeGen(Module& M) : M_(M), C_(M.getContext()), B_(C_) {}

  void run(const Program& prog) {
    for (const auto& f : prog.filters) {
      Function* pred = emitPredicate("f$" + f.name, f.lets, f.body.get());
      emitKernels("f$" + f.name, pred, f.schema->size);
    }
    static const std::vector<LetStmt> kNoLets;
    for (const auto& q : prog.queries) {
      if (!q.where) continue;
      Function* pred = emitPredicate("q$" + q.name, kNoLets, q.where.get());
      emitKernels("q$" + q.name, pred, q.schema->size);
    }
  }

private:
  Module& M_;
  LLVMContext& C_;
  IRBuilder<> B_;
  std::vector<CGVal> letVals_;
  Value* rec_ = nullptr;
  std::map<std::string, GlobalVariable*> strGlobals_;

  Type* i1() { return B_.getInt1Ty(); }
  Type* i64() { return B_.getInt64Ty(); }
  Type* ptrTy() { return PointerType::get(C_, 0); }

  Type* scalarTy(Ty t) {
    switch (t) {
      case Ty::Bool: return B_.getInt1Ty();
      case Ty::U8: case Ty::I8: return B_.getInt8Ty();
      case Ty::U16: case Ty::I16: return B_.getInt16Ty();
      case Ty::U32: case Ty::I32: case Ty::IP4: return B_.getInt32Ty();
      case Ty::U64: case Ty::I64: return B_.getInt64Ty();
      case Ty::F64: return B_.getDoubleTy();
      default: fail("internal: no scalar LLVM type for str");
    }
  }

  FunctionCallee strRT(const char* name) {
    auto* fnTy = FunctionType::get(B_.getInt32Ty(), {ptrTy(), i64(), ptrTy(), i64()}, false);
    FunctionCallee callee = M_.getOrInsertFunction(name, fnTy);
    if (auto* F = dyn_cast<Function>(callee.getCallee())) {
      F->setMemoryEffects(MemoryEffects::readOnly());
      F->addFnAttr(Attribute::NoUnwind);
      F->addFnAttr(Attribute::WillReturn);
    }
    return callee;
  }

  Function* emitPredicate(const std::string& sym, const std::vector<LetStmt>& lets,
                          const Expr* body) {
    auto* fnTy = FunctionType::get(i1(), {ptrTy()}, false);
    auto* F = Function::Create(fnTy, Function::ExternalLinkage, sym, M_);
    F->addFnAttr(Attribute::AlwaysInline);
    F->addFnAttr(Attribute::NoUnwind);
    F->addRetAttr(Attribute::ZExt);
    F->addParamAttr(0, Attribute::ReadOnly);
    F->addParamAttr(0, Attribute::NonNull);

    auto* entry = BasicBlock::Create(C_, "entry", F);
    B_.SetInsertPoint(entry);
    rec_ = F->getArg(0);
    rec_->setName("rec");

    letVals_.clear();
    for (const auto& let : lets) {
      CGVal v = emit(let.init.get());
      if (v.v) v.v->setName(let.name);
      letVals_.push_back(v);
    }

    B_.CreateRet(emit(body).v);
    return F;
  }

  void emitKernels(const std::string& sym, Function* pred, uint32_t stride) {
    emitCount(sym + "$count", pred, stride);
    emitCollect(sym + "$collect", pred, stride);
  }

  void emitCount(const std::string& sym, Function* pred, uint32_t stride) {
    auto* fnTy = FunctionType::get(i64(), {ptrTy(), i64()}, false);
    auto* F = Function::Create(fnTy, Function::ExternalLinkage, sym, M_);
    F->addFnAttr(Attribute::NoUnwind);
    Value* base = F->getArg(0);
    Value* n = F->getArg(1);
    base->setName("base");
    n->setName("n");

    auto* entry = BasicBlock::Create(C_, "entry", F);
    auto* loop = BasicBlock::Create(C_, "loop", F);
    auto* exit = BasicBlock::Create(C_, "exit", F);

    B_.SetInsertPoint(entry);
    Value* empty = B_.CreateICmpEQ(n, B_.getInt64(0));
    B_.CreateCondBr(empty, exit, loop);

    B_.SetInsertPoint(loop);
    PHINode* i = B_.CreatePHI(i64(), 2, "i");
    PHINode* acc = B_.CreatePHI(i64(), 2, "acc");
    i->addIncoming(B_.getInt64(0), entry);
    acc->addIncoming(B_.getInt64(0), entry);
    Value* off = B_.CreateMul(i, B_.getInt64(stride));
    Value* p = B_.CreateInBoundsGEP(B_.getInt8Ty(), base, off);
    Value* m = B_.CreateCall(pred, {p});
    Value* acc2 = B_.CreateAdd(acc, B_.CreateZExt(m, i64()));
    Value* inext = B_.CreateAdd(i, B_.getInt64(1));
    BasicBlock* loopEnd = B_.GetInsertBlock();
    i->addIncoming(inext, loopEnd);
    acc->addIncoming(acc2, loopEnd);
    Value* done = B_.CreateICmpEQ(inext, n);
    B_.CreateCondBr(done, exit, loop);

    B_.SetInsertPoint(exit);
    PHINode* res = B_.CreatePHI(i64(), 2, "res");
    res->addIncoming(B_.getInt64(0), entry);
    res->addIncoming(acc2, loopEnd);
    B_.CreateRet(res);
  }

  void emitCollect(const std::string& sym, Function* pred, uint32_t stride) {
    auto* fnTy = FunctionType::get(i64(), {ptrTy(), i64(), ptrTy(), i64()}, false);
    auto* F = Function::Create(fnTy, Function::ExternalLinkage, sym, M_);
    F->addFnAttr(Attribute::NoUnwind);
    Value* base = F->getArg(0);
    Value* n = F->getArg(1);
    Value* out = F->getArg(2);
    Value* cap = F->getArg(3);
    base->setName("base");
    n->setName("n");
    out->setName("out");
    cap->setName("cap");

    auto* entry = BasicBlock::Create(C_, "entry", F);
    auto* head = BasicBlock::Create(C_, "head", F);
    auto* body = BasicBlock::Create(C_, "body", F);
    auto* store = BasicBlock::Create(C_, "store", F);
    auto* latch = BasicBlock::Create(C_, "latch", F);
    auto* exit = BasicBlock::Create(C_, "exit", F);

    B_.SetInsertPoint(entry);
    B_.CreateBr(head);

    B_.SetInsertPoint(head);
    PHINode* i = B_.CreatePHI(i64(), 2, "i");
    PHINode* cnt = B_.CreatePHI(i64(), 2, "cnt");
    i->addIncoming(B_.getInt64(0), entry);
    cnt->addIncoming(B_.getInt64(0), entry);
    Value* more = B_.CreateICmpULT(i, n);
    Value* room = B_.CreateICmpULT(cnt, cap);
    B_.CreateCondBr(B_.CreateAnd(more, room), body, exit);

    B_.SetInsertPoint(body);
    Value* off = B_.CreateMul(i, B_.getInt64(stride));
    Value* p = B_.CreateInBoundsGEP(B_.getInt8Ty(), base, off);
    Value* m = B_.CreateCall(pred, {p});
    BasicBlock* bodyEnd = B_.GetInsertBlock();
    B_.CreateCondBr(m, store, latch);

    B_.SetInsertPoint(store);
    Value* slot = B_.CreateInBoundsGEP(i64(), out, cnt);
    B_.CreateStore(i, slot);
    Value* cntInc = B_.CreateAdd(cnt, B_.getInt64(1));
    B_.CreateBr(latch);

    B_.SetInsertPoint(latch);
    PHINode* cnt2 = B_.CreatePHI(i64(), 2, "cnt2");
    cnt2->addIncoming(cnt, bodyEnd);
    cnt2->addIncoming(cntInc, store);
    Value* inext = B_.CreateAdd(i, B_.getInt64(1));
    i->addIncoming(inext, latch);
    cnt->addIncoming(cnt2, latch);
    B_.CreateBr(head);

    B_.SetInsertPoint(exit);
    B_.CreateRet(cnt);
  }

  CGVal strLiteral(const std::string& s) {
    GlobalVariable*& gv = strGlobals_[s];
    if (!gv) {
      auto* arr = ConstantDataArray::getString(C_, s, /*AddNull=*/false);
      gv = new GlobalVariable(M_, arr->getType(), /*constant=*/true,
                              GlobalValue::PrivateLinkage, arr, ".str");
      gv->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    }
    return {gv, B_.getInt64(s.size())};
  }

  Value* toI64(Value* v, Ty from) {
    if (v->getType()->isIntegerTy(64)) return v;
    return isUnsignedTy(from) || from == Ty::Bool ? B_.CreateZExt(v, i64())
                                                  : B_.CreateSExt(v, i64());
  }

  Value* toF64(Value* v, Ty from) {
    if (from == Ty::F64) return v;
    return isUnsignedTy(from) ? B_.CreateUIToFP(v, B_.getDoubleTy())
                              : B_.CreateSIToFP(v, B_.getDoubleTy());
  }

  Value* callStr(const char* fn, CGVal a, CGVal b) {
    Value* r = B_.CreateCall(strRT(fn), {a.v, a.len, b.v, b.len});
    return B_.CreateICmpNE(r, B_.getInt32(0));
  }

  CGVal emit(const Expr* e) {
    switch (e->kind) {
      case ExprKind::IntLit:
        return {B_.getInt64((uint64_t)static_cast<const IntLitExpr*>(e)->v)};
      case ExprKind::FloatLit:
        return {ConstantFP::get(B_.getDoubleTy(), static_cast<const FloatLitExpr*>(e)->v)};
      case ExprKind::StrLit:
        return strLiteral(static_cast<const StrLitExpr*>(e)->v);
      case ExprKind::BoolLit:
        return {B_.getInt1(static_cast<const BoolLitExpr*>(e)->v)};
      case ExprKind::IpLit:
        return {B_.getInt32(static_cast<const IpLitExpr*>(e)->addr)};
      case ExprKind::Var:
        return letVals_[static_cast<const VarExpr*>(e)->letIndex];
      case ExprKind::Field: return emitField(static_cast<const FieldExpr*>(e));
      case ExprKind::Unary: return emitUnary(static_cast<const UnaryExpr*>(e));
      case ExprKind::Binary: return emitBinary(static_cast<const BinaryExpr*>(e));
      case ExprKind::Between: return emitBetween(static_cast<const BetweenExpr*>(e));
      case ExprKind::InList: return emitInList(static_cast<const InListExpr*>(e));
      case ExprKind::InCidr: return emitInCidr(static_cast<const InCidrExpr*>(e));
      case ExprKind::StrOp: return emitStrOp(static_cast<const StrOpExpr*>(e));
      case ExprKind::Len:
        return {emit(static_cast<const LenExpr*>(e)->arg.get()).len};
    }
    fail("internal: unhandled expression in codegen");
  }

  CGVal emitField(const FieldExpr* f) {
    const FieldInfo& fi = *f->fi;
    Value* addr =
        B_.CreateConstInBoundsGEP1_64(B_.getInt8Ty(), rec_, fi.offset, fi.name);
    switch (fi.ty) {
      case Ty::Bool: {
        Value* b = B_.CreateAlignedLoad(B_.getInt8Ty(), addr, Align(1));
        return {B_.CreateICmpNE(b, B_.getInt8(0))};
      }
      case Ty::Str: {
        Value* sp = B_.CreateAlignedLoad(ptrTy(), addr, Align(8));
        Value* lenAddr = B_.CreateConstInBoundsGEP1_64(B_.getInt8Ty(), addr, 8);
        Value* sl = B_.CreateAlignedLoad(i64(), lenAddr, Align(8));
        return {sp, sl};
      }
      default:
        return {B_.CreateAlignedLoad(scalarTy(fi.ty), addr, Align(fi.align))};
    }
  }

  CGVal emitUnary(const UnaryExpr* u) {
    CGVal v = emit(u->operand.get());
    switch (u->op) {
      case UnOp::Not: return {B_.CreateNot(v.v)};
      case UnOp::Neg:
        if (u->operand->type == Ty::F64) return {B_.CreateFNeg(v.v)};
        return {B_.CreateNeg(toI64(v.v, u->operand->type))};
      case UnOp::BitNot: return {B_.CreateNot(toI64(v.v, u->operand->type))};
    }
    fail("internal: bad unary op");
  }

  Value* emitCmp(BinOp op, CGVal l, Ty lt, CGVal r, Ty rt) {
    if (lt == Ty::Str) {
      Value* eq = callStr("nql_str_eq", l, r);
      return op == BinOp::Eq ? eq : B_.CreateNot(eq);
    }
    if (lt == Ty::IP4 || (lt == Ty::Bool && rt == Ty::Bool)) {
      return op == BinOp::Eq ? B_.CreateICmpEQ(l.v, r.v) : B_.CreateICmpNE(l.v, r.v);
    }
    Promo p = promote(lt, rt);
    if (p.isFloat) {
      Value* a = toF64(l.v, lt);
      Value* b = toF64(r.v, rt);
      switch (op) {
        case BinOp::Eq: return B_.CreateFCmpOEQ(a, b);
        case BinOp::Ne: return B_.CreateFCmpUNE(a, b);
        case BinOp::Lt: return B_.CreateFCmpOLT(a, b);
        case BinOp::Le: return B_.CreateFCmpOLE(a, b);
        case BinOp::Gt: return B_.CreateFCmpOGT(a, b);
        case BinOp::Ge: return B_.CreateFCmpOGE(a, b);
        default: break;
      }
    }
    Value* a = toI64(l.v, lt);
    Value* b = toI64(r.v, rt);
    switch (op) {
      case BinOp::Eq: return B_.CreateICmpEQ(a, b);
      case BinOp::Ne: return B_.CreateICmpNE(a, b);
      case BinOp::Lt: return p.isUnsigned ? B_.CreateICmpULT(a, b) : B_.CreateICmpSLT(a, b);
      case BinOp::Le: return p.isUnsigned ? B_.CreateICmpULE(a, b) : B_.CreateICmpSLE(a, b);
      case BinOp::Gt: return p.isUnsigned ? B_.CreateICmpUGT(a, b) : B_.CreateICmpSGT(a, b);
      case BinOp::Ge: return p.isUnsigned ? B_.CreateICmpUGE(a, b) : B_.CreateICmpSGE(a, b);
      default: break;
    }
    fail("internal: bad comparison op");
  }

  // safe to emit branchless (no trapping/expensive ops): no runtime calls, no math that can trap
  static bool speculatable(const Expr* e) {
    switch (e->kind) {
      case ExprKind::IntLit: case ExprKind::FloatLit: case ExprKind::BoolLit:
      case ExprKind::IpLit: case ExprKind::StrLit: case ExprKind::Var:
      case ExprKind::Field:
        return true;
      case ExprKind::Len:
        return true;
      case ExprKind::Unary:
        return speculatable(static_cast<const UnaryExpr*>(e)->operand.get());
      case ExprKind::Binary: {
        auto* b = static_cast<const BinaryExpr*>(e);
        if (b->lhs->type == Ty::Str) return false; // str eq/ne calls the runtime
        if (b->op == BinOp::Div || b->op == BinOp::Mod) {
          int64_t divisor;
          bool constNonZero = b->rhs->kind == ExprKind::IntLit &&
                              (divisor = static_cast<const IntLitExpr*>(b->rhs.get())->v) != 0;
          if (!constNonZero && b->type != Ty::F64) return false; // may trap
        }
        return speculatable(b->lhs.get()) && speculatable(b->rhs.get());
      }
      case ExprKind::Between: {
        auto* b = static_cast<const BetweenExpr*>(e);
        return speculatable(b->subject.get()) && speculatable(b->lo.get()) &&
               speculatable(b->hi.get());
      }
      case ExprKind::InList: {
        auto* i = static_cast<const InListExpr*>(e);
        return i->subject->type != Ty::Str && speculatable(i->subject.get());
      }
      case ExprKind::InCidr:
        return speculatable(static_cast<const InCidrExpr*>(e)->subject.get());
      case ExprKind::StrOp:
        return false;
    }
    return false;
  }

  CGVal emitBinary(const BinaryExpr* bin) {
    if (bin->op == BinOp::And || bin->op == BinOp::Or) {
      bool isAnd = bin->op == BinOp::And;
      Value* l = emit(bin->lhs.get()).v;
      if (speculatable(bin->rhs.get())) {
        Value* r = emit(bin->rhs.get()).v;
        return {isAnd ? B_.CreateAnd(l, r) : B_.CreateOr(l, r)};
      }
      Function* F = B_.GetInsertBlock()->getParent();
      BasicBlock* fromBB = B_.GetInsertBlock();
      auto* rhsBB = BasicBlock::Create(C_, isAnd ? "and.rhs" : "or.rhs", F);
      auto* endBB = BasicBlock::Create(C_, isAnd ? "and.end" : "or.end", F);
      if (isAnd) B_.CreateCondBr(l, rhsBB, endBB);
      else B_.CreateCondBr(l, endBB, rhsBB);
      B_.SetInsertPoint(rhsBB);
      Value* r = emit(bin->rhs.get()).v;
      BasicBlock* rhsEnd = B_.GetInsertBlock();
      B_.CreateBr(endBB);
      B_.SetInsertPoint(endBB);
      PHINode* phi = B_.CreatePHI(i1(), 2);
      phi->addIncoming(B_.getInt1(!isAnd), fromBB);
      phi->addIncoming(r, rhsEnd);
      return {phi};
    }

    CGVal l = emit(bin->lhs.get());
    CGVal r = emit(bin->rhs.get());
    Ty lt = bin->lhs->type, rt = bin->rhs->type;

    switch (bin->op) {
      case BinOp::Eq: case BinOp::Ne:
      case BinOp::Lt: case BinOp::Le:
      case BinOp::Gt: case BinOp::Ge:
        return {emitCmp(bin->op, l, lt, r, rt)};
      default: break;
    }

    Promo p = promote(lt, rt);
    if (p.isFloat) {
      Value* a = toF64(l.v, lt);
      Value* b = toF64(r.v, rt);
      switch (bin->op) {
        case BinOp::Add: return {B_.CreateFAdd(a, b)};
        case BinOp::Sub: return {B_.CreateFSub(a, b)};
        case BinOp::Mul: return {B_.CreateFMul(a, b)};
        case BinOp::Div: return {B_.CreateFDiv(a, b)};
        default: fail("internal: bad float op");
      }
    }

    Value* a = toI64(l.v, lt);
    Value* b = toI64(r.v, rt);
    switch (bin->op) {
      case BinOp::Add: return {B_.CreateAdd(a, b)};
      case BinOp::Sub: return {B_.CreateSub(a, b)};
      case BinOp::Mul: return {B_.CreateMul(a, b)};
      case BinOp::Div: return {p.isUnsigned ? B_.CreateUDiv(a, b) : B_.CreateSDiv(a, b)};
      case BinOp::Mod: return {p.isUnsigned ? B_.CreateURem(a, b) : B_.CreateSRem(a, b)};
      case BinOp::BitAnd: return {B_.CreateAnd(a, b)};
      case BinOp::BitOr: return {B_.CreateOr(a, b)};
      case BinOp::BitXor: return {B_.CreateXor(a, b)};
      case BinOp::Shl: return {B_.CreateShl(a, b)};
      case BinOp::Shr: return {p.isUnsigned ? B_.CreateLShr(a, b) : B_.CreateAShr(a, b)};
      default: fail("internal: bad binary op");
    }
  }

  CGVal emitBetween(const BetweenExpr* b) {
    CGVal s = emit(b->subject.get());
    CGVal lo = emit(b->lo.get());
    CGVal hi = emit(b->hi.get());
    Value* c1 = emitCmp(BinOp::Le, lo, b->lo->type, s, b->subject->type);
    Value* c2 = emitCmp(BinOp::Le, s, b->subject->type, hi, b->hi->type);
    return {B_.CreateAnd(c1, c2)};
  }

  CGVal emitInList(const InListExpr* il) {
    CGVal s = emit(il->subject.get());
    Ty st = il->subject->type;
    Value* any = B_.getInt1(false);
    for (const auto& el : il->elems) {
      CGVal v = emit(el.get());
      Value* eq = emitCmp(BinOp::Eq, s, st, v, el->type);
      any = B_.CreateOr(any, eq);
    }
    return {il->negated ? B_.CreateNot(any) : any};
  }

  CGVal emitInCidr(const InCidrExpr* ic) {
    Value* ip = emit(ic->subject.get()).v;
    uint32_t mask = ic->mask();
    Value* masked = B_.CreateAnd(ip, B_.getInt32(mask));
    Value* in = B_.CreateICmpEQ(masked, B_.getInt32(ic->net & mask));
    return {ic->negated ? B_.CreateNot(in) : in};
  }

  CGVal emitStrOp(const StrOpExpr* s) {
    CGVal a = emit(s->subject.get());
    CGVal b = emit(s->pattern.get());
    const char* fn;
    switch (s->op) {
      case StrOpKind::Contains: fn = "nql_str_contains"; break;
      case StrOpKind::StartsWith: fn = "nql_str_starts"; break;
      case StrOpKind::EndsWith: fn = "nql_str_ends"; break;
      case StrOpKind::Matches: fn = "nql_str_glob"; break;
    }
    return {callStr(fn, a, b)};
  }
};

} // namespace

void emitProgram(const Program& prog, llvm::Module& M) { CodeGen(M).run(prog); }

} // namespace nql
