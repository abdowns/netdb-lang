#include "parser.hpp"

#include <unordered_map>

#include "lexer.hpp"

namespace nql {
namespace {

// unlike c, bitwise ops bind tighter than comparisons: a & 1 == 1 is (a & 1) == 1
class Parser {
public:
  explicit Parser(std::string_view src) : toks_(lex(src)) {}

  Program run() {
    Program prog;
    while (!at(Tok::Eof)) {
      switch (cur().kind) {
        case Tok::KwSchema: parseSchema(prog); break;
        case Tok::KwConst: parseConst(); break;
        case Tok::KwFilter: parseFilter(prog); break;
        default:
          fail(cur().loc, std::string("expected 'schema', 'const' or 'filter', got ") + tokName(cur().kind));
      }
    }
    return prog;
  }

private:
  std::vector<Token> toks_;
  size_t pos_ = 0;
  std::unordered_map<std::string, Token> consts_;

  const Token& cur() const { return toks_[pos_]; }
  bool at(Tok k) const { return cur().kind == k; }
  Token eat() { return toks_[pos_++]; }
  Token expect(Tok k, const char* what) {
    if (!at(k))
      fail(cur().loc, std::string("expected ") + tokName(k) + " (" + what + "), got " + tokName(cur().kind));
    return eat();
  }
  bool accept(Tok k) {
    if (at(k)) { eat(); return true; }
    return false;
  }

  void parseSchema(Program& prog) {
    eat();
    Token name = expect(Tok::Ident, "schema name");
    if (prog.findSchema(name.text))
      fail(name.loc, "redefinition of schema '" + name.text + "'");
    auto schema = std::make_unique<Schema>();
    schema->name = name.text;
    expect(Tok::LBrace, "schema body");
    while (!at(Tok::RBrace)) {
      Token fname = expect(Tok::Ident, "field name");
      expect(Tok::Colon, "after field name");
      Token tname = expect(Tok::Ident, "field type");
      Ty ty = tyFromName(tname.text);
      if (ty == Ty::Invalid)
        fail(tname.loc, "unknown type '" + tname.text + "'");
      if (schema->field(fname.text))
        fail(fname.loc, "duplicate field '" + fname.text + "'");
      schema->fields.push_back({fname.text, ty});
      accept(Tok::Comma) || accept(Tok::Semi);
    }
    eat();
    schema->layout();
    prog.schemas.push_back(std::move(schema));
  }

  void parseConst() {
    eat();
    Token name = expect(Tok::Ident, "const name");
    expect(Tok::Assign, "after const name");
    bool neg = accept(Tok::Minus);
    Token val = eat();
    if (val.kind == Tok::IntLit) {
      if (neg) val.ival = -val.ival;
    } else if (val.kind == Tok::FloatLit) {
      if (neg) val.fval = -val.fval;
    } else if (val.kind == Tok::StrLit || val.kind == Tok::KwTrue || val.kind == Tok::KwFalse) {
      if (neg) fail(val.loc, "cannot negate this literal");
    } else {
      fail(val.loc, "const value must be a literal");
    }
    consts_[name.text] = val;
    expect(Tok::Semi, "after const declaration");
  }

  void parseFilter(Program& prog) {
    eat();
    FilterDecl f;
    f.loc = cur().loc;
    f.name = expect(Tok::Ident, "filter name").text;
    expect(Tok::LParen, "filter parameter list");
    f.paramName = expect(Tok::Ident, "parameter name").text;
    expect(Tok::Colon, "after parameter name");
    f.schemaName = expect(Tok::Ident, "schema type").text;
    expect(Tok::RParen, "after parameter");
    if (accept(Tok::Arrow)) {
      Token rt = expect(Tok::Ident, "return type");
      if (rt.text != "bool")
        fail(rt.loc, "filters must return bool");
    }
    expect(Tok::LBrace, "filter body");
    f.body = parseExpr();
    expect(Tok::RBrace, "end of filter body");
    prog.filters.push_back(std::move(f));
  }

  ExprPtr parseExpr() { return parseOr(); }

  template <typename E, typename... A>
  ExprPtr mk(SrcLoc loc, A&&... args) {
    auto e = std::make_unique<E>(std::forward<A>(args)...);
    e->loc = loc;
    return e;
  }

  ExprPtr parseOr() {
    ExprPtr lhs = parseAnd();
    while (at(Tok::KwOr)) {
      SrcLoc loc = eat().loc;
      lhs = mk<BinaryExpr>(loc, BinOp::Or, std::move(lhs), parseAnd());
    }
    return lhs;
  }

  ExprPtr parseAnd() {
    ExprPtr lhs = parseNot();
    while (at(Tok::KwAnd)) {
      SrcLoc loc = eat().loc;
      lhs = mk<BinaryExpr>(loc, BinOp::And, std::move(lhs), parseNot());
    }
    return lhs;
  }

  ExprPtr parseNot() {
    if (at(Tok::KwNot)) {
      SrcLoc loc = eat().loc;
      return mk<UnaryExpr>(loc, UnOp::Not, parseNot());
    }
    return parseComparison();
  }

  ExprPtr parseComparison() {
    ExprPtr lhs = parseBitOr();

    if (at(Tok::KwContains) || at(Tok::KwStartswith) || at(Tok::KwEndswith) || at(Tok::KwMatches)) {
      Token op = eat();
      StrOpKind k = op.kind == Tok::KwContains     ? StrOpKind::Contains
                    : op.kind == Tok::KwStartswith ? StrOpKind::StartsWith
                    : op.kind == Tok::KwEndswith   ? StrOpKind::EndsWith
                                                   : StrOpKind::Matches;
      return mk<StrOpExpr>(op.loc, k, std::move(lhs), parseBitOr());
    }

    BinOp op;
    switch (cur().kind) {
      case Tok::Eq: op = BinOp::Eq; break;
      case Tok::Ne: op = BinOp::Ne; break;
      case Tok::Lt: op = BinOp::Lt; break;
      case Tok::Le: op = BinOp::Le; break;
      case Tok::Gt: op = BinOp::Gt; break;
      case Tok::Ge: op = BinOp::Ge; break;
      default: return lhs;
    }
    SrcLoc loc = eat().loc;
    return mk<BinaryExpr>(loc, op, std::move(lhs), parseBitOr());
  }

  ExprPtr parseBitOr() {
    ExprPtr lhs = parseBitXor();
    while (at(Tok::Pipe)) {
      SrcLoc loc = eat().loc;
      lhs = mk<BinaryExpr>(loc, BinOp::BitOr, std::move(lhs), parseBitXor());
    }
    return lhs;
  }

  ExprPtr parseBitXor() {
    ExprPtr lhs = parseBitAnd();
    while (at(Tok::Caret)) {
      SrcLoc loc = eat().loc;
      lhs = mk<BinaryExpr>(loc, BinOp::BitXor, std::move(lhs), parseBitAnd());
    }
    return lhs;
  }

  ExprPtr parseBitAnd() {
    ExprPtr lhs = parseShift();
    while (at(Tok::Amp)) {
      SrcLoc loc = eat().loc;
      lhs = mk<BinaryExpr>(loc, BinOp::BitAnd, std::move(lhs), parseShift());
    }
    return lhs;
  }

  ExprPtr parseShift() {
    ExprPtr lhs = parseAdd();
    while (at(Tok::Shl) || at(Tok::Shr)) {
      BinOp op = at(Tok::Shl) ? BinOp::Shl : BinOp::Shr;
      SrcLoc loc = eat().loc;
      lhs = mk<BinaryExpr>(loc, op, std::move(lhs), parseAdd());
    }
    return lhs;
  }

  ExprPtr parseAdd() {
    ExprPtr lhs = parseMul();
    while (at(Tok::Plus) || at(Tok::Minus)) {
      BinOp op = at(Tok::Plus) ? BinOp::Add : BinOp::Sub;
      SrcLoc loc = eat().loc;
      lhs = mk<BinaryExpr>(loc, op, std::move(lhs), parseMul());
    }
    return lhs;
  }

  ExprPtr parseMul() {
    ExprPtr lhs = parseUnary();
    while (at(Tok::Star) || at(Tok::Slash) || at(Tok::Percent)) {
      BinOp op = at(Tok::Star) ? BinOp::Mul : at(Tok::Slash) ? BinOp::Div : BinOp::Mod;
      SrcLoc loc = eat().loc;
      lhs = mk<BinaryExpr>(loc, op, std::move(lhs), parseUnary());
    }
    return lhs;
  }

  ExprPtr parseUnary() {
    if (at(Tok::Minus)) {
      SrcLoc loc = eat().loc;
      return mk<UnaryExpr>(loc, UnOp::Neg, parseUnary());
    }
    if (at(Tok::Tilde)) {
      SrcLoc loc = eat().loc;
      return mk<UnaryExpr>(loc, UnOp::BitNot, parseUnary());
    }
    return parsePostfix();
  }

  ExprPtr parsePostfix() {
    ExprPtr e = parsePrimary();
    while (at(Tok::Dot)) {
      SrcLoc loc = eat().loc;
      Token field = expect(Tok::Ident, "field name after '.'");
      if (e->kind != ExprKind::Var)
        fail(loc, "field access is only valid on the record parameter");
      auto* var = static_cast<VarExpr*>(e.get());
      e = mk<FieldExpr>(loc, var->name, field.text);
    }
    return e;
  }

  ExprPtr parsePrimary() {
    Token t = cur();
    switch (t.kind) {
      case Tok::IntLit: eat(); return mk<IntLitExpr>(t.loc, t.ival);
      case Tok::FloatLit: eat(); return mk<FloatLitExpr>(t.loc, t.fval);
      case Tok::StrLit: eat(); return mk<StrLitExpr>(t.loc, t.text);
      case Tok::KwTrue: eat(); return mk<BoolLitExpr>(t.loc, true);
      case Tok::KwFalse: eat(); return mk<BoolLitExpr>(t.loc, false);
      case Tok::LParen: {
        eat();
        ExprPtr e = parseExpr();
        expect(Tok::RParen, "closing ')'");
        return e;
      }
      case Tok::Ident: {
        eat();
        auto it = consts_.find(t.text);
        if (it != consts_.end()) {
          const Token& c = it->second;
          switch (c.kind) {
            case Tok::IntLit: return mk<IntLitExpr>(t.loc, c.ival);
            case Tok::FloatLit: return mk<FloatLitExpr>(t.loc, c.fval);
            case Tok::StrLit: return mk<StrLitExpr>(t.loc, c.text);
            case Tok::KwTrue: return mk<BoolLitExpr>(t.loc, true);
            case Tok::KwFalse: return mk<BoolLitExpr>(t.loc, false);
            default: fail(t.loc, "bad const");
          }
        }
        return mk<VarExpr>(t.loc, t.text); // param name or bare field: sema decides
      }
      default:
        fail(t.loc, std::string("expected an expression, got ") + tokName(t.kind));
    }
  }
};

} // namespace

Program parse(std::string_view src) { return Parser(src).run(); }

static const char* binOpName(BinOp op) {
  switch (op) {
    case BinOp::Or: return "or";
    case BinOp::And: return "and";
    case BinOp::Eq: return "==";
    case BinOp::Ne: return "!=";
    case BinOp::Lt: return "<";
    case BinOp::Le: return "<=";
    case BinOp::Gt: return ">";
    case BinOp::Ge: return ">=";
    case BinOp::Add: return "+";
    case BinOp::Sub: return "-";
    case BinOp::Mul: return "*";
    case BinOp::Div: return "/";
    case BinOp::Mod: return "%";
    case BinOp::BitAnd: return "&";
    case BinOp::BitOr: return "|";
    case BinOp::BitXor: return "^";
    case BinOp::Shl: return "<<";
    case BinOp::Shr: return ">>";
  }
  return "?";
}

std::string exprToString(const Expr* e) {
  switch (e->kind) {
    case ExprKind::IntLit: return std::to_string(static_cast<const IntLitExpr*>(e)->v);
    case ExprKind::FloatLit: return std::to_string(static_cast<const FloatLitExpr*>(e)->v);
    case ExprKind::StrLit: return "\"" + static_cast<const StrLitExpr*>(e)->v + "\"";
    case ExprKind::BoolLit: return static_cast<const BoolLitExpr*>(e)->v ? "true" : "false";
    case ExprKind::Var: return static_cast<const VarExpr*>(e)->name;
    case ExprKind::Field: {
      auto* f = static_cast<const FieldExpr*>(e);
      return f->recName + "." + f->fieldName;
    }
    case ExprKind::Unary: {
      auto* u = static_cast<const UnaryExpr*>(e);
      const char* op = u->op == UnOp::Not ? "not " : u->op == UnOp::Neg ? "-" : "~";
      return std::string(op) + exprToString(u->operand.get());
    }
    case ExprKind::Binary: {
      auto* b = static_cast<const BinaryExpr*>(e);
      return "(" + exprToString(b->lhs.get()) + " " + binOpName(b->op) + " " +
             exprToString(b->rhs.get()) + ")";
    }
    case ExprKind::StrOp: {
      auto* s = static_cast<const StrOpExpr*>(e);
      const char* op = s->op == StrOpKind::Contains     ? "contains"
                       : s->op == StrOpKind::StartsWith ? "startswith"
                       : s->op == StrOpKind::EndsWith   ? "endswith"
                                                        : "matches";
      return "(" + exprToString(s->subject.get()) + " " + op + " " +
             exprToString(s->pattern.get()) + ")";
    }
  }
  return "?";
}

} // namespace nql
