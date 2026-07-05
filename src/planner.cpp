#include "planner.hpp"

#include <algorithm>

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

void fold(ExprPtr& e);

void foldChildren(Expr* e) {
  switch (e->kind) {
    case ExprKind::Unary: fold(static_cast<UnaryExpr*>(e)->operand); break;
    case ExprKind::Binary: {
      auto* b = static_cast<BinaryExpr*>(e);
      fold(b->lhs);
      fold(b->rhs);
      break;
    }
    case ExprKind::Between: {
      auto* b = static_cast<BetweenExpr*>(e);
      fold(b->subject);
      fold(b->lo);
      fold(b->hi);
      break;
    }
    case ExprKind::InList: fold(static_cast<InListExpr*>(e)->subject); break;
    case ExprKind::InCidr: fold(static_cast<InCidrExpr*>(e)->subject); break;
    case ExprKind::StrOp: {
      auto* s = static_cast<StrOpExpr*>(e);
      fold(s->subject);
      fold(s->pattern);
      break;
    }
    case ExprKind::Len: fold(static_cast<LenExpr*>(e)->arg); break;
    default: break;
  }
}

void fold(ExprPtr& e) {
  foldChildren(e.get());

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

  bool bv;
  if (bin->op == BinOp::And) {
    if (isBoolLit(bin->lhs.get(), bv)) { e = bv ? std::move(bin->rhs) : boolLit(false, e->loc); return; }
    if (isBoolLit(bin->rhs.get(), bv)) { e = bv ? std::move(bin->lhs) : boolLit(false, e->loc); return; }
    return;
  }
  if (bin->op == BinOp::Or) {
    if (isBoolLit(bin->lhs.get(), bv)) { e = bv ? boolLit(true, e->loc) : std::move(bin->rhs); return; }
    if (isBoolLit(bin->rhs.get(), bv)) { e = bv ? boolLit(true, e->loc) : std::move(bin->lhs); return; }
    return;
  }

  int64_t l, r;
  if (isIntLit(bin->lhs.get(), l) && isIntLit(bin->rhs.get(), r)) {
    bool uns = promote(bin->lhs->type, bin->rhs->type).isUnsigned;
    uint64_t ul = (uint64_t)l, ur = (uint64_t)r;
    switch (bin->op) {
      case BinOp::Add: e = intLit(l + r, e->loc, e->type); return;
      case BinOp::Sub: e = intLit(l - r, e->loc, e->type); return;
      case BinOp::Mul: e = intLit(l * r, e->loc, e->type); return;
      case BinOp::Div:
        if (r == 0) fail(e->loc, "division by zero in constant expression");
        e = intLit(uns ? (int64_t)(ul / ur) : l / r, e->loc, e->type);
        return;
      case BinOp::Mod:
        if (r == 0) fail(e->loc, "modulo by zero in constant expression");
        e = intLit(uns ? (int64_t)(ul % ur) : l % r, e->loc, e->type);
        return;
      case BinOp::BitAnd: e = intLit(l & r, e->loc, e->type); return;
      case BinOp::BitOr: e = intLit(l | r, e->loc, e->type); return;
      case BinOp::BitXor: e = intLit(l ^ r, e->loc, e->type); return;
      case BinOp::Shl: e = intLit((int64_t)(ul << (ur & 63)), e->loc, e->type); return;
      case BinOp::Shr:
        e = intLit(uns ? (int64_t)(ul >> (ur & 63)) : l >> (r & 63), e->loc, e->type);
        return;
      case BinOp::Eq: e = boolLit(l == r, e->loc); return;
      case BinOp::Ne: e = boolLit(l != r, e->loc); return;
      case BinOp::Lt: e = boolLit(uns ? ul < ur : l < r, e->loc); return;
      case BinOp::Le: e = boolLit(uns ? ul <= ur : l <= r, e->loc); return;
      case BinOp::Gt: e = boolLit(uns ? ul > ur : l > r, e->loc); return;
      case BinOp::Ge: e = boolLit(uns ? ul >= ur : l >= r, e->loc); return;
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

void flattenAnd(ExprPtr e, std::vector<ExprPtr>& out) {
  if (e->kind == ExprKind::Binary) {
    auto* b = static_cast<BinaryExpr*>(e.get());
    if (b->op == BinOp::And) {
      flattenAnd(std::move(b->lhs), out);
      flattenAnd(std::move(b->rhs), out);
      return;
    }
  }
  out.push_back(std::move(e));
}

void reorderConjuncts(ExprPtr& e) {
  if (e->kind != ExprKind::Binary ||
      static_cast<BinaryExpr*>(e.get())->op != BinOp::And)
    return;

  std::vector<ExprPtr> conjuncts;
  flattenAnd(std::move(e), conjuncts);

  std::stable_sort(conjuncts.begin(), conjuncts.end(),
                   [](const ExprPtr& a, const ExprPtr& b) {
                     return exprCost(a.get()) < exprCost(b.get());
                   });

  ExprPtr acc = std::move(conjuncts[0]);
  for (size_t i = 1; i < conjuncts.size(); i++) {
    SrcLoc loc = conjuncts[i]->loc;
    auto b = std::make_unique<BinaryExpr>(BinOp::And, std::move(acc), std::move(conjuncts[i]));
    b->loc = loc;
    b->type = Ty::Bool;
    acc = std::move(b);
  }
  e = std::move(acc);
}

void planExpr(ExprPtr& e) {
  fold(e);
  reorderConjuncts(e);
}

} // namespace

int exprCost(const Expr* e) {
  switch (e->kind) {
    case ExprKind::IntLit:
    case ExprKind::FloatLit:
    case ExprKind::BoolLit:
    case ExprKind::IpLit:
    case ExprKind::StrLit:
    case ExprKind::Var:
      return 0;
    case ExprKind::Field:
      return 1;
    case ExprKind::Len:
      return 1 + exprCost(static_cast<const LenExpr*>(e)->arg.get());
    case ExprKind::Unary:
      return exprCost(static_cast<const UnaryExpr*>(e)->operand.get());
    case ExprKind::Binary: {
      auto* b = static_cast<const BinaryExpr*>(e);
      int base = 1;
      if (b->op == BinOp::Eq || b->op == BinOp::Ne) {
        if (b->lhs->type == Ty::Str) base = 8; // runtime call + memcmp
      }
      if (b->op == BinOp::Div || b->op == BinOp::Mod) base = 4;
      return base + exprCost(b->lhs.get()) + exprCost(b->rhs.get());
    }
    case ExprKind::Between: {
      auto* b = static_cast<const BetweenExpr*>(e);
      return 2 + exprCost(b->subject.get()) + exprCost(b->lo.get()) + exprCost(b->hi.get());
    }
    case ExprKind::InList: {
      auto* i = static_cast<const InListExpr*>(e);
      int per = i->subject->type == Ty::Str ? 8 : 1;
      return (int)i->elems.size() * per + exprCost(i->subject.get());
    }
    case ExprKind::InCidr:
      return 2 + exprCost(static_cast<const InCidrExpr*>(e)->subject.get());
    case ExprKind::StrOp: {
      auto* s = static_cast<const StrOpExpr*>(e);
      int base;
      switch (s->op) {
        case StrOpKind::StartsWith:
        case StrOpKind::EndsWith: base = 6; break;
        case StrOpKind::Contains: base = 16; break;
        case StrOpKind::Matches: base = 32; break;
      }
      return base + exprCost(s->subject.get()) + exprCost(s->pattern.get());
    }
  }
  return 1;
}

void plan(Program& prog) {
  for (auto& f : prog.filters) {
    for (auto& let : f.lets) fold(let.init);
    planExpr(f.body);
  }
  for (auto& q : prog.queries)
    if (q.where) planExpr(q.where);
}

std::string explainPlan(const Program& prog) {
  std::string out;
  auto describe = [&](const std::string& kind, const std::string& name, const Expr* body) {
    out += kind + " " + name + ":\n";
    std::vector<const Expr*> conjuncts;
    const Expr* cur = body;
    while (cur->kind == ExprKind::Binary &&
           static_cast<const BinaryExpr*>(cur)->op == BinOp::And) {
      auto* b = static_cast<const BinaryExpr*>(cur);
      conjuncts.push_back(b->rhs.get());
      cur = b->lhs.get();
    }
    conjuncts.push_back(cur);
    std::reverse(conjuncts.begin(), conjuncts.end());
    if (conjuncts.size() == 1) {
      out += "  predicate (cost " + std::to_string(exprCost(body)) + "): " + exprToString(body) + "\n";
    } else {
      for (size_t i = 0; i < conjuncts.size(); i++) {
        out += "  step " + std::to_string(i + 1) + " (cost " +
               std::to_string(exprCost(conjuncts[i])) + "): " + exprToString(conjuncts[i]) + "\n";
      }
      out += "  => cheapest conjunct first; later steps only run when earlier ones pass\n";
    }
  };
  for (const auto& f : prog.filters) describe("filter", f.name, f.body.get());
  for (const auto& q : prog.queries)
    if (q.where) describe("query", q.name, q.where.get());
  return out;
}

} // namespace nql
