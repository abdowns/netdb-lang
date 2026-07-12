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

  static double asF64(const Value& v) {
    if (v.ty == Ty::F64) return v.f;
    return isUnsignedTy(v.ty) ? (double)v.u : (double)v.i;
  }

  Value eval(const Expr* e) {
    switch (e->kind) {
      case ExprKind::IntLit: return mkInt(static_cast<const IntLitExpr*>(e)->v, e->type);
      case ExprKind::FloatLit: {
        Value v;
        v.ty = Ty::F64;
        v.f = static_cast<const FloatLitExpr*>(e)->v;
        return v;
      }
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
          case UnOp::Neg:
            if (v.ty == Ty::F64) {
              v.f = -v.f;
              return v;
            }
            return mkInt(-v.i, e->type);
          case UnOp::BitNot: return mkInt(~v.i, e->type);
        }
        return v;
      }

      case ExprKind::Binary: return evalBinary(static_cast<const BinaryExpr*>(e));

      case ExprKind::Between: {
        const auto* b = static_cast<const BetweenExpr*>(e);
        Value s = eval(b->subject.get());
        Value lo = eval(b->lo.get());
        Value hi = eval(b->hi.get());
        return mkBool(cmpLe(lo, s) && cmpLe(s, hi));
      }

      case ExprKind::InList: {
        const auto* i = static_cast<const InListExpr*>(e);
        Value s = eval(i->subject.get());
        bool found = false;
        for (const auto& el : i->elems) {
          Value v = eval(el.get());
          if (valueEq(s, v)) {
            found = true;
            break;
          }
        }
        return mkBool(found != i->negated);
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

      case ExprKind::Len: {
        const auto* l = static_cast<const LenExpr*>(e);
        Value v = eval(l->arg.get());
        return mkInt((int64_t)v.s.len, Ty::U64);
      }
    }
    return mkBool(false);
  }

  static bool valueEq(const Value& a, const Value& b) {
    if (a.ty == Ty::Str) return nql_str_eq(a.s.ptr, a.s.len, b.s.ptr, b.s.len);
    if (a.ty == Ty::IP4) return a.ip == b.ip;
    Promo p = promote(a.ty, b.ty);
    if (p.isFloat) return asF64(a) == asF64(b);
    return a.i == b.i; // same 64 bit pattern regardless of signedness
  }

  static bool cmpLe(const Value& a, const Value& b) {
    Promo p = promote(a.ty, b.ty);
    if (p.isFloat) return asF64(a) <= asF64(b);
    if (p.isUnsigned) return a.u <= b.u;
    return a.i <= b.i;
  }

  Value evalBinary(const BinaryExpr* b) {
    if (b->op == BinOp::And) {
      Value l = eval(b->lhs.get());
      if (l.i == 0) return mkBool(false);
      return mkBool(eval(b->rhs.get()).i != 0);
    }
    if (b->op == BinOp::Or) {
      Value l = eval(b->lhs.get());
      if (l.i != 0) return mkBool(true);
      return mkBool(eval(b->rhs.get()).i != 0);
    }

    Value l = eval(b->lhs.get());
    Value r = eval(b->rhs.get());

    if (b->op == BinOp::Eq) return mkBool(valueEq(l, r));
    if (b->op == BinOp::Ne) return mkBool(!valueEq(l, r));

    Promo p = promote(l.ty, r.ty);

    switch (b->op) {
      case BinOp::Lt:
      case BinOp::Le:
      case BinOp::Gt:
      case BinOp::Ge: {
        int c;
        if (p.isFloat) {
          double a = asF64(l), bb = asF64(r);
          c = a < bb ? -1 : a > bb ? 1 : 0;
        } else if (p.isUnsigned) {
          c = l.u < r.u ? -1 : l.u > r.u ? 1 : 0;
        } else {
          c = l.i < r.i ? -1 : l.i > r.i ? 1 : 0;
        }
        switch (b->op) {
          case BinOp::Lt: return mkBool(c < 0);
          case BinOp::Le: return mkBool(c <= 0);
          case BinOp::Gt: return mkBool(c > 0);
          default: return mkBool(c >= 0);
        }
      }

      case BinOp::Add:
      case BinOp::Sub:
      case BinOp::Mul:
      case BinOp::Div:
      case BinOp::Mod: {
        if (p.isFloat) {
          double a = asF64(l), bb = asF64(r);
          Value v;
          v.ty = Ty::F64;
          switch (b->op) {
            case BinOp::Add: v.f = a + bb; break;
            case BinOp::Sub: v.f = a - bb; break;
            case BinOp::Mul: v.f = a * bb; break;
            default: v.f = a / bb; break;
          }
          return v;
        }
        int64_t out;
        switch (b->op) {
          case BinOp::Add: out = l.i + r.i; break;
          case BinOp::Sub: out = l.i - r.i; break;
          case BinOp::Mul: out = l.i * r.i; break;
          case BinOp::Div:
            if (r.i == 0) fail(b->loc, "division by zero at runtime");
            out = p.isUnsigned ? (int64_t)(l.u / r.u) : l.i / r.i;
            break;
          default:
            if (r.i == 0) fail(b->loc, "modulo by zero at runtime");
            out = p.isUnsigned ? (int64_t)(l.u % r.u) : l.i % r.i;
            break;
        }
        return mkInt(out, b->type);
      }

      case BinOp::BitAnd: return mkInt(l.i & r.i, b->type);
      case BinOp::BitOr: return mkInt(l.i | r.i, b->type);
      case BinOp::BitXor: return mkInt(l.i ^ r.i, b->type);
      case BinOp::Shl: return mkInt((int64_t)(l.u << (r.u & 63)), b->type);
      case BinOp::Shr:
        return mkInt(p.isUnsigned ? (int64_t)(l.u >> (r.u & 63)) : l.i >> (r.i & 63), b->type);

      default: return mkBool(false);
    }
  }
};

} // namespace

bool evalPredicate(const Expr* body, const std::vector<LetStmt>& lets, const uint8_t* rec) {
  return Interp(lets, rec).run(body);
}

} // namespace nql
