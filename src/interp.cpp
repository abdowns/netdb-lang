#include "interp.hpp"

#include "rt.hpp"

namespace nql {
namespace {

struct Value {
  Ty ty = Ty::Invalid;
  union {
    int64_t i;
    uint64_t u;
    double f;
    uint32_t ip;
    StrRef s;
  };
};

class Interp {
public:
  Interp(const std::vector<LetStmt>& lets, const uint8_t* rec) : rec_(rec) {
    letVals_.reserve(lets.size());
    for (const auto& l : lets) letVals_.push_back(eval(l.init.get()));
  }

  bool run(const Expr* body) { return eval(body).i != 0; }

private:
  const uint8_t* rec_;
  std::vector<Value> letVals_;

  static Value mkBool(bool b) {
    Value v;
    v.ty = Ty::Bool;
    v.i = b;
    return v;
  }

  static Value mkInt(int64_t x, Ty ty) {
    Value v;
    v.ty = ty;
    v.i = x;
    return v;
  }

  static Value mkF64(double x) {
    Value v;
    v.ty = Ty::F64;
    v.f = x;
    return v;
  }

  static double asF64(const Value& v) { return v.ty == Ty::F64 ? v.f : (double)v.i; }

  Value eval(const Expr* e) {
    switch (e->kind) {
      case ExprKind::IntLit: return mkInt(static_cast<const IntLitExpr*>(e)->v, e->type);
      case ExprKind::FloatLit: return mkF64(static_cast<const FloatLitExpr*>(e)->v);
      case ExprKind::StrLit: {
        const auto* s = static_cast<const StrLitExpr*>(e);
        Value v;
        v.ty = Ty::Str;
        v.s = {s->v.data(), s->v.size()};
        return v;
      }
      case ExprKind::BoolLit: return mkBool(static_cast<const BoolLitExpr*>(e)->v);
      case ExprKind::IpLit: {
        Value v;
        v.ty = Ty::IP4;
        v.ip = static_cast<const IpLitExpr*>(e)->addr;
        return v;
      }
      case ExprKind::Var:
        return letVals_[static_cast<const VarExpr*>(e)->letIndex];

      case ExprKind::Field: {
        const FieldInfo& fi = *static_cast<const FieldExpr*>(e)->fi;
        Value v;
        v.ty = fi.ty;
        switch (fi.ty) {
          case Ty::Bool: v.i = loadUInt(rec_, fi) != 0; break;
          case Ty::F64: v.f = loadF64(rec_, fi); break;
          case Ty::Str: v.s = loadStr(rec_, fi); break;
          case Ty::IP4: v.ip = (uint32_t)loadUInt(rec_, fi); break;
          default:
            if (isUnsignedTy(fi.ty)) v.u = loadUInt(rec_, fi);
            else v.i = loadSInt(rec_, fi);
        }
        return v;
      }

      case ExprKind::Unary: {
        const auto* u = static_cast<const UnaryExpr*>(e);
        Value v = eval(u->operand.get());
        switch (u->op) {
          case UnOp::Not: return mkBool(v.i == 0);
          case UnOp::Neg: return v.ty == Ty::F64 ? mkF64(-v.f) : mkInt(-v.i, e->type);
          case UnOp::BitNot: return mkInt(~v.i, e->type);
        }
        return v;
      }

      case ExprKind::Binary: return evalBinary(static_cast<const BinaryExpr*>(e));

      case ExprKind::InList: {
        const auto* i = static_cast<const InListExpr*>(e);
        Value s = eval(i->subject.get());
        for (const auto& el : i->elems)
          if (valueEq(s, eval(el.get()))) return mkBool(!i->negated);
        return mkBool(i->negated);
      }

      case ExprKind::InCidr: {
        const auto* i = static_cast<const InCidrExpr*>(e);
        Value s = eval(i->subject.get());
        bool in = (s.ip & i->mask()) == (i->net & i->mask());
        return mkBool(in != i->negated);
      }

      case ExprKind::StrOp: {
        const auto* s = static_cast<const StrOpExpr*>(e);
        Value a = eval(s->subject.get());
        Value b = eval(s->pattern.get());
        int32_t r = 0;
        switch (s->op) {
          case StrOpKind::Contains: r = nql_str_contains(a.s.ptr, a.s.len, b.s.ptr, b.s.len); break;
          case StrOpKind::StartsWith: r = nql_str_starts(a.s.ptr, a.s.len, b.s.ptr, b.s.len); break;
          case StrOpKind::EndsWith: r = nql_str_ends(a.s.ptr, a.s.len, b.s.ptr, b.s.len); break;
          case StrOpKind::Matches: r = nql_str_glob(a.s.ptr, a.s.len, b.s.ptr, b.s.len); break;
        }
        return mkBool(r != 0);
      }

      default: fail(e->loc, "interpreter: unsupported expression");
    }
    return mkBool(false);
  }

  static bool valueEq(const Value& a, const Value& b) {
    if (a.ty == Ty::Str) return nql_str_eq(a.s.ptr, a.s.len, b.s.ptr, b.s.len);
    if (a.ty == Ty::IP4) return a.ip == b.ip;
    if (a.ty == Ty::F64 || b.ty == Ty::F64) return asF64(a) == asF64(b);
    return a.i == b.i;
  }

  static int cmp(const Value& a, const Value& b) {
    if (a.ty == Ty::F64 || b.ty == Ty::F64) {
      double x = asF64(a), y = asF64(b);
      return x < y ? -1 : x > y ? 1 : 0;
    }
    if (isUnsignedTy(a.ty)) return a.u < b.u ? -1 : a.u > b.u ? 1 : 0;
    return a.i < b.i ? -1 : a.i > b.i ? 1 : 0;
  }

  Value evalBinary(const BinaryExpr* b) {
    Value l = eval(b->lhs.get());
    if (b->op == BinOp::And && l.i == 0) return mkBool(false);
    if (b->op == BinOp::Or && l.i != 0) return mkBool(true);
    Value r = eval(b->rhs.get());

    bool isF = l.ty == Ty::F64 || r.ty == Ty::F64;
    bool isU = isUnsignedTy(l.ty);

    switch (b->op) {
      case BinOp::And:
      case BinOp::Or: return mkBool(r.i != 0);
      case BinOp::Eq: return mkBool(valueEq(l, r));
      case BinOp::Ne: return mkBool(!valueEq(l, r));
      case BinOp::Lt: return mkBool(cmp(l, r) < 0);
      case BinOp::Le: return mkBool(cmp(l, r) <= 0);
      case BinOp::Gt: return mkBool(cmp(l, r) > 0);
      case BinOp::Ge: return mkBool(cmp(l, r) >= 0);
      case BinOp::Add: return isF ? mkF64(asF64(l) + asF64(r)) : mkInt(l.i + r.i, b->type);
      case BinOp::Sub: return isF ? mkF64(asF64(l) - asF64(r)) : mkInt(l.i - r.i, b->type);
      case BinOp::Mul: return isF ? mkF64(asF64(l) * asF64(r)) : mkInt(l.i * r.i, b->type);
      case BinOp::Div:
        if (isF) return mkF64(asF64(l) / asF64(r));
        if (r.i == 0) fail(b->loc, "division by zero at runtime");
        return mkInt(isU ? (int64_t)(l.u / r.u) : l.i / r.i, b->type);
      case BinOp::Mod:
        if (r.i == 0) fail(b->loc, "modulo by zero at runtime");
        return mkInt(isU ? (int64_t)(l.u % r.u) : l.i % r.i, b->type);
      case BinOp::BitAnd: return mkInt(l.i & r.i, b->type);
      case BinOp::BitOr: return mkInt(l.i | r.i, b->type);
      case BinOp::BitXor: return mkInt(l.i ^ r.i, b->type);
      case BinOp::Shl: return mkInt((int64_t)(l.u << (r.u & 63)), b->type);
      case BinOp::Shr: return mkInt(isU ? (int64_t)(l.u >> (r.u & 63)) : l.i >> (r.i & 63), b->type);
    }
    return mkBool(false);
  }
};

} // namespace

bool evalPredicate(const Expr* body, const std::vector<LetStmt>& lets, const uint8_t* rec) {
  return Interp(lets, rec).run(body);
}

} // namespace nql
