#include "sema.hpp"

namespace nql {
namespace {

struct Scope {
  const Schema* schema = nullptr;
  std::string paramName;
  std::vector<const LetStmt*> lets;
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
    for (auto& let : f.lets) {
      if (let.name == f.paramName)
        fail(let.loc, "let '" + let.name + "' shadows the record parameter");
      for (const auto* prev : scope.lets)
        if (prev->name == let.name)
          fail(let.loc, "redefinition of let '" + let.name + "'");
      if (f.schema->field(let.name))
        fail(let.loc, "let '" + let.name + "' shadows a field of " + f.schema->name);
      let.type = check(let.init, scope);
      scope.lets.push_back(&let);
    }
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
        for (size_t i = 0; i < scope.lets.size(); i++) {
          if (scope.lets[i]->name == v->name) {
            v->letIndex = (int)i;
            return scope.lets[i]->type;
          }
        }
        if (const FieldInfo* fi = scope.schema->field(v->name)) {
          auto fe = std::make_unique<FieldExpr>("", v->name);
          fe->loc = e->loc;
          fe->fi = fi;
          e = std::move(fe);
          return fi->ty;
        }
        if (v->name == scope.paramName)
          fail(e->loc, "record '" + v->name + "' used as a value; access a field like '" +
                           v->name + ".<field>'");
        fail(e->loc, "unknown identifier '" + v->name + "'");
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

      case ExprKind::Between: {
        auto* b = static_cast<BetweenExpr*>(e.get());
        Ty st = check(b->subject, scope);
        Ty lt = check(b->lo, scope);
        Ty ht = check(b->hi, scope);
        if (!isNumericTy(st) || !isNumericTy(lt) || !isNumericTy(ht))
          fail(e->loc, "'between' needs numeric operands");
        return Ty::Bool;
      }

      case ExprKind::InList: {
        auto* i = static_cast<InListExpr*>(e.get());
        Ty st = check(i->subject, scope);
        for (auto& el : i->elems) {
          Ty et = check(el, scope);
          bool ok = (isNumericTy(st) && isNumericTy(et)) ||
                    (st == Ty::Str && et == Ty::Str) ||
                    (st == Ty::IP4 && et == Ty::IP4);
          if (!ok)
            fail(el->loc, std::string("list element type ") + tyName(et) +
                              " does not match subject type " + tyName(st));
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

      case ExprKind::Len: {
        auto* l = static_cast<LenExpr*>(e.get());
        Ty t = check(l->arg, scope);
        if (t != Ty::Str) fail(e->loc, std::string("len() needs a str, got ") + tyName(t));
        return Ty::U64;
      }
    }
    return Ty::Invalid;
  }
};

} // namespace

void analyze(Program& prog) { Checker(prog).run(); }

} // namespace nql
