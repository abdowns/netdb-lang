#include "planner.hpp"

namespace nql {
namespace {

bool isIntLit(const Expr* e, int64_t& out) {
  if (e->kind == ExprKind::IntLit) {
    out = static_cast<const IntLitExpr*>(e)->v;
    return true;
  }
  return false;
}

bool isBoolLit(const Expr* e, bool& out) {
  if (e->kind == ExprKind::BoolLit) {
    out = static_cast<const BoolLitExpr*>(e)->v;
    return true;
  }
  return false;
}

bool isFloatLit(const Expr* e, double& out) {
  if (e->kind == ExprKind::FloatLit) {
    out = static_cast<const FloatLitExpr*>(e)->v;
    return true;
  }
  return false;
}

ExprPtr intLit(int64_t v, SrcLoc loc, Ty ty) {
  auto e = std::make_unique<IntLitExpr>(v);
  e->loc = loc;
  e->type = ty;
  return e;
}

ExprPtr boolLit(bool v, SrcLoc loc) {
  auto e = std::make_unique<BoolLitExpr>(v);
  e->loc = loc;
  e->type = Ty::Bool;
  return e;
}

ExprPtr floatLit(double v, SrcLoc loc) {
  auto e = std::make_unique<FloatLitExpr>(v);
  e->loc = loc;
  e->type = Ty::F64;
  return e;
}

void fold(ExprPtr& e) {
  switch (e->kind) {
    case ExprKind::Unary: fold(static_cast<UnaryExpr*>(e.get())->operand); break;
    case ExprKind::Binary: {
      auto* b = static_cast<BinaryExpr*>(e.get());
      fold(b->lhs);
      fold(b->rhs);
      break;
    }
    case ExprKind::InList: fold(static_cast<InListExpr*>(e.get())->subject); break;
    case ExprKind::InCidr: fold(static_cast<InCidrExpr*>(e.get())->subject); break;
    case ExprKind::StrOp: {
      auto* s = static_cast<StrOpExpr*>(e.get());
      fold(s->subject);
      fold(s->pattern);
      break;
    }
    default: break;
  }

  if (e->kind == ExprKind::Unary) {
    auto* u = static_cast<UnaryExpr*>(e.get());
    int64_t i;
    bool b;
    double f;
    if (u->op == UnOp::Not && isBoolLit(u->operand.get(), b)) { e = boolLit(!b, e->loc); return; }
    if (u->op == UnOp::Neg && isIntLit(u->operand.get(), i)) { e = intLit(-i, e->loc, e->type); return; }
    if (u->op == UnOp::Neg && isFloatLit(u->operand.get(), f)) { e = floatLit(-f, e->loc); return; }
    if (u->op == UnOp::BitNot && isIntLit(u->operand.get(), i)) { e = intLit(~i, e->loc, e->type); return; }
    return;
  }

  if (e->kind != ExprKind::Binary) return;
  auto* bin = static_cast<BinaryExpr*>(e.get());

  bool lb, rb;
  if (isBoolLit(bin->lhs.get(), lb) && isBoolLit(bin->rhs.get(), rb)) {
    if (bin->op == BinOp::And) { e = boolLit(lb && rb, e->loc); return; }
    if (bin->op == BinOp::Or) { e = boolLit(lb || rb, e->loc); return; }
    return;
  }

  int64_t l, r;
  if (isIntLit(bin->lhs.get(), l) && isIntLit(bin->rhs.get(), r)) {
    switch (bin->op) {
      case BinOp::Add: e = intLit(l + r, e->loc, e->type); return;
      case BinOp::Sub: e = intLit(l - r, e->loc, e->type); return;
      case BinOp::Mul: e = intLit(l * r, e->loc, e->type); return;
      case BinOp::Div:
        if (r == 0) fail(e->loc, "division by zero in constant expression");
        e = intLit(l / r, e->loc, e->type);
        return;
      case BinOp::Mod:
        if (r == 0) fail(e->loc, "modulo by zero in constant expression");
        e = intLit(l % r, e->loc, e->type);
        return;
      case BinOp::BitAnd: e = intLit(l & r, e->loc, e->type); return;
      case BinOp::BitOr: e = intLit(l | r, e->loc, e->type); return;
      case BinOp::BitXor: e = intLit(l ^ r, e->loc, e->type); return;
      case BinOp::Shl: e = intLit(l << (r & 63), e->loc, e->type); return;
      case BinOp::Shr: e = intLit(l >> (r & 63), e->loc, e->type); return;
      case BinOp::Eq: e = boolLit(l == r, e->loc); return;
      case BinOp::Ne: e = boolLit(l != r, e->loc); return;
      case BinOp::Lt: e = boolLit(l < r, e->loc); return;
      case BinOp::Le: e = boolLit(l <= r, e->loc); return;
      case BinOp::Gt: e = boolLit(l > r, e->loc); return;
      case BinOp::Ge: e = boolLit(l >= r, e->loc); return;
      default: return;
    }
  }

  double fl, fr;
  if (isFloatLit(bin->lhs.get(), fl) && isFloatLit(bin->rhs.get(), fr)) {
    switch (bin->op) {
      case BinOp::Add: e = floatLit(fl + fr, e->loc); return;
      case BinOp::Sub: e = floatLit(fl - fr, e->loc); return;
      case BinOp::Mul: e = floatLit(fl * fr, e->loc); return;
      case BinOp::Div: e = floatLit(fl / fr, e->loc); return;
      case BinOp::Eq: e = boolLit(fl == fr, e->loc); return;
      case BinOp::Ne: e = boolLit(fl != fr, e->loc); return;
      case BinOp::Lt: e = boolLit(fl < fr, e->loc); return;
      case BinOp::Le: e = boolLit(fl <= fr, e->loc); return;
      case BinOp::Gt: e = boolLit(fl > fr, e->loc); return;
      case BinOp::Ge: e = boolLit(fl >= fr, e->loc); return;
      default: return;
    }
  }
}

} // namespace

void plan(Program& prog) {
  for (auto& f : prog.filters) {
    for (auto& let : f.lets) fold(let.init);
    fold(f.body);
  }
  for (auto& q : prog.queries)
    if (q.where) fold(q.where);
}

} // namespace nql
