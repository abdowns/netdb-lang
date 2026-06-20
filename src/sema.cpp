#include "sema.hpp"

namespace nql {
namespace {

struct Scope {
  const Schema* schema = nullptr;
  std::string paramName;
};

// takes ExprPtr& to rewrite bare field identifiers into FieldExpr in place
class Checker {
public:
  explicit Checker(Program& prog) : prog_(prog) {}

  void run() {
    for (auto& f : prog_.filters) checkFilter(f);
    for (auto& q : prog_.queries) checkQuery(q);
  }

private:
  Program& prog_;

  const Schema* resolveSchema(const std::string& name, SrcLoc loc) {
    const Schema* s = prog_.findSchema(name);
    if (!s) fail(loc, "unknown schema '" + name + "'");
    return s;
  }

  void checkFilter(FilterDecl& f) {
    f.schema = resolveSchema(f.schemaName, f.loc);
    Scope scope;
    scope.schema = f.schema;
    scope.paramName = f.paramName;
    Ty t = check(f.body, scope);
    if (t != Ty::Bool)
      fail(f.body->loc, "filter '" + f.name + "' body must be bool, got " + std::string(tyName(t)));
  }

  void checkQuery(QueryDecl& q) {
    q.schema = resolveSchema(q.schemaName, q.loc);
    Scope scope;
    scope.schema = q.schema;
    if (q.where) {
      Ty t = check(q.where, scope);
      if (t != Ty::Bool)
        fail(q.where->loc, "'where' clause must be bool, got " + std::string(tyName(t)));
    }
    for (const auto& name : q.selectFields) {
      const FieldInfo* fi = q.schema->field(name);
      if (!fi) fail(q.loc, "select: no field '" + name + "' in schema " + q.schema->name);
      q.selectInfo.push_back(fi);
    }
    if (!q.orderField.empty()) {
      q.orderInfo = q.schema->field(q.orderField);
      if (!q.orderInfo)
        fail(q.loc, "order by: no field '" + q.orderField + "' in schema " + q.schema->name);
    }
  }

  Ty check(ExprPtr& e, const Scope& scope) {
    Ty t = checkImpl(e, scope);
    e->type = t;
    return t;
  }

  Ty checkImpl(ExprPtr& e, const Scope& scope) {
    switch (e->kind) {
      case ExprKind::IntLit: return Ty::I64;
      case ExprKind::FloatLit: return Ty::F64;
      case ExprKind::StrLit: return Ty::Str;
      case ExprKind::BoolLit: return Ty::Bool;
      case ExprKind::IpLit: return Ty::IP4;

      case ExprKind::Var: {
        auto* v = static_cast<VarExpr*>(e.get());
        const FieldInfo* fi = scope.schema->field(v->name);
        if (!fi) fail(e->loc, "unknown identifier '" + v->name + "'");
        auto fe = std::make_unique<FieldExpr>("", v->name);
        fe->loc = e->loc;
        fe->fi = fi;
        e = std::move(fe);
        return fi->ty;
      }

      case ExprKind::Field: {
        auto* f = static_cast<FieldExpr*>(e.get());
        if (!f->recName.empty() && f->recName != scope.paramName)
          fail(e->loc, "unknown record '" + f->recName + "'");
        f->fi = scope.schema->field(f->fieldName);
        if (!f->fi)
          fail(e->loc, "schema " + scope.schema->name + " has no field '" + f->fieldName + "'");
        return f->fi->ty;
      }

      case ExprKind::Unary: {
        auto* u = static_cast<UnaryExpr*>(e.get());
        Ty t = check(u->operand, scope);
        switch (u->op) {
          case UnOp::Not:
            if (t != Ty::Bool) fail(e->loc, std::string("'not' needs bool, got ") + tyName(t));
            return Ty::Bool;
          case UnOp::Neg:
            if (!isNumericTy(t)) fail(e->loc, std::string("'-' needs a number, got ") + tyName(t));
            return t == Ty::F64 ? Ty::F64 : Ty::I64;
          case UnOp::BitNot:
            if (!isIntTy(t)) fail(e->loc, std::string("'~' needs an integer, got ") + tyName(t));
            return t == Ty::U64 ? Ty::U64 : Ty::I64;
        }
        return Ty::Invalid;
      }

      case ExprKind::Binary: {
        auto* b = static_cast<BinaryExpr*>(e.get());
        Ty lt = check(b->lhs, scope);
        Ty rt = check(b->rhs, scope);
        switch (b->op) {
          case BinOp::And:
          case BinOp::Or:
            if (lt != Ty::Bool || rt != Ty::Bool)
              fail(e->loc, std::string("'") + (b->op == BinOp::And ? "and" : "or") +
                               "' needs bool operands, got " + tyName(lt) + " and " + tyName(rt));
            return Ty::Bool;

          case BinOp::Eq:
          case BinOp::Ne:
            if (lt == Ty::Str && rt == Ty::Str) return Ty::Bool;
            if (lt == Ty::IP4 && rt == Ty::IP4) return Ty::Bool;
            if (lt == Ty::Bool && rt == Ty::Bool) return Ty::Bool;
            [[fallthrough]];
          case BinOp::Lt:
          case BinOp::Le:
          case BinOp::Gt:
          case BinOp::Ge:
            if (!isNumericTy(lt) || !isNumericTy(rt))
              fail(e->loc, std::string("cannot compare ") + tyName(lt) + " with " + tyName(rt));
            return Ty::Bool;

          case BinOp::Add:
          case BinOp::Sub:
          case BinOp::Mul:
          case BinOp::Div:
          case BinOp::Mod: {
            if (!isNumericTy(lt) || !isNumericTy(rt))
              fail(e->loc, std::string("arithmetic needs numbers, got ") + tyName(lt) + " and " +
                               tyName(rt));
            Promo p = promote(lt, rt);
            if (p.isFloat) {
              if (b->op == BinOp::Mod) fail(e->loc, "'%' is not defined for f64");
              return Ty::F64;
            }
            return p.isUnsigned ? Ty::U64 : Ty::I64;
          }

          case BinOp::BitAnd:
          case BinOp::BitOr:
          case BinOp::BitXor:
          case BinOp::Shl:
          case BinOp::Shr: {
            if (!isIntTy(lt) || !isIntTy(rt))
              fail(e->loc, std::string("bitwise ops need integers, got ") + tyName(lt) + " and " +
                               tyName(rt));
            Promo p = promote(lt, rt);
            return p.isUnsigned ? Ty::U64 : Ty::I64;
          }
        }
        return Ty::Invalid;
      }

      case ExprKind::InList: {
        auto* i = static_cast<InListExpr*>(e.get());
        Ty st = check(i->subject, scope);
        for (auto& el : i->elems) {
          Ty et = check(el, scope);
          bool ok = st == et || (isNumericTy(st) && isNumericTy(et));
          if (!ok) fail(el->loc, "list element type does not match subject");
        }
        return Ty::Bool;
      }

      case ExprKind::InCidr: {
        auto* i = static_cast<InCidrExpr*>(e.get());
        Ty st = check(i->subject, scope);
        if (st != Ty::IP4)
          fail(e->loc, std::string("CIDR match needs an ip4 subject, got ") + tyName(st));
        return Ty::Bool;
      }

      case ExprKind::StrOp: {
        auto* s = static_cast<StrOpExpr*>(e.get());
        Ty st = check(s->subject, scope);
        Ty pt = check(s->pattern, scope);
        if (st != Ty::Str || pt != Ty::Str)
          fail(e->loc, std::string("string operator needs str operands, got ") + tyName(st) +
                           " and " + tyName(pt));
        return Ty::Bool;
      }
    }
    return Ty::Invalid;
  }
};

} // namespace

void analyze(Program& prog) { Checker(prog).run(); }

} // namespace nql
