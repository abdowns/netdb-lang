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
      if (at(Tok::KwSchema)) parseSchema(prog);
      else if (at(Tok::KwConst)) parseConst();
      else if (at(Tok::KwFilter)) parseFilter(prog);
      else if (at(Tok::KwQuery)) parseQuery(prog);
      else fail(cur().loc, std::string("expected 'schema', 'const', 'filter' or 'query', got ") + tokName(cur().kind));
    }
    return prog;
  }

private:
  std::vector<Token> toks_;
  size_t pos_ = 0;
  std::unordered_map<std::string, Token> consts_;

  const Token& cur() const { return toks_[pos_]; }
  const Token& peek(size_t off = 1) const {
    return toks_[std::min(pos_ + off, toks_.size() - 1)];
  }
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
    switch (val.kind) {
      case Tok::IntLit:
        if (neg) val.ival = -val.ival;
        break;
      case Tok::FloatLit:
        if (neg) val.fval = -val.fval;
        break;
      case Tok::StrLit:
      case Tok::IpLit:
      case Tok::KwTrue:
      case Tok::KwFalse:
        if (neg) fail(val.loc, "cannot negate this literal");
        break;
      default:
        fail(val.loc, "const value must be a literal");
    }
    consts_[name.text] = val;
    expect(Tok::Semi, "after const declaration");
  }

  void parseFilter(Program& prog) {
    eat();
    FilterDecl f;
    Token name = expect(Tok::Ident, "filter name");
    f.name = name.text;
    f.loc = name.loc;
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
    while (at(Tok::KwLet)) {
      eat();
      LetStmt let;
      Token ln = expect(Tok::Ident, "let name");
      let.name = ln.text;
      let.loc = ln.loc;
      expect(Tok::Assign, "after let name");
      let.init = parseExpr();
      expect(Tok::Semi, "after let binding");
      f.lets.push_back(std::move(let));
    }
    f.body = parseExpr();
    expect(Tok::RBrace, "end of filter body");
    prog.filters.push_back(std::move(f));
  }

  void parseQuery(Program& prog) {
    eat();
    QueryDecl q;
    Token name = expect(Tok::Ident, "query name");
    q.name = name.text;
    q.loc = name.loc;
    expect(Tok::KwOver, "after query name");
    q.schemaName = expect(Tok::Ident, "schema name").text;
    expect(Tok::LBrace, "query body");
    while (!at(Tok::RBrace)) {
      if (accept(Tok::KwWhere)) {
        if (q.where) fail(cur().loc, "duplicate 'where' clause");
        q.where = parseExpr();
      } else if (accept(Tok::KwSelect)) {
        if (!q.selectFields.empty()) fail(cur().loc, "duplicate 'select' clause");
        do {
          q.selectFields.push_back(expect(Tok::Ident, "field name").text);
        } while (accept(Tok::Comma));
      } else if (accept(Tok::KwOrder)) {
        if (!q.orderField.empty()) fail(cur().loc, "duplicate 'order by' clause");
        expect(Tok::KwBy, "after 'order'");
        q.orderField = expect(Tok::Ident, "field name").text;
        if (accept(Tok::KwDesc)) q.orderDesc = true;
        else accept(Tok::KwAsc);
      } else if (accept(Tok::KwLimit)) {
        if (q.limit >= 0) fail(cur().loc, "duplicate 'limit' clause");
        Token n = expect(Tok::IntLit, "limit count");
        if (n.ival < 0) fail(n.loc, "limit must be non-negative");
        q.limit = n.ival;
      } else {
        fail(cur().loc, std::string("expected 'where', 'select', 'order by' or 'limit', got ") + tokName(cur().kind));
      }
      accept(Tok::Semi);
    }
    eat();
    prog.queries.push_back(std::move(q));
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

    bool negated = false;
    if (at(Tok::KwNot) && peek().kind == Tok::KwIn) {
      eat();
      negated = true;
    }

    if (at(Tok::KwIn)) {
      SrcLoc loc = eat().loc;
      return parseInRhs(loc, std::move(lhs), negated);
    }
    if (negated) fail(cur().loc, "expected 'in' after 'not'");

    if (at(Tok::KwBetween)) {
      SrcLoc loc = eat().loc;
      ExprPtr lo = parseBitOr();
      expect(Tok::KwAnd, "in 'between x and y'");
      ExprPtr hi = parseBitOr();
      return mk<BetweenExpr>(loc, std::move(lhs), std::move(lo), std::move(hi));
    }

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

  ExprPtr parseInRhs(SrcLoc loc, ExprPtr subject, bool negated) {
    if (at(Tok::LBracket)) {
      eat();
      auto e = std::make_unique<InListExpr>(std::move(subject));
      e->loc = loc;
      e->negated = negated;
      if (!at(Tok::RBracket)) {
        do {
          e->elems.push_back(parseLiteral("list element"));
        } while (accept(Tok::Comma));
      }
      expect(Tok::RBracket, "end of list");
      if (e->elems.empty()) fail(loc, "'in []' with an empty list");
      return e;
    }
    if (at(Tok::IpLit) || (at(Tok::Ident) && constKind(cur().text) == Tok::IpLit)) {
      Token ip = eat();
      if (ip.kind == Tok::Ident) ip = consts_.at(ip.text);
      int prefix = 32;
      if (accept(Tok::Slash)) {
        Token p = expect(Tok::IntLit, "CIDR prefix length");
        if (p.ival < 0 || p.ival > 32) fail(p.loc, "CIDR prefix must be 0..32");
        prefix = (int)p.ival;
      }
      auto e = std::make_unique<InCidrExpr>(std::move(subject), ip.ip, prefix);
      e->loc = loc;
      e->negated = negated;
      return e;
    }
    fail(cur().loc, "expected a list [...] or CIDR block after 'in'");
  }

  Tok constKind(const std::string& name) const {
    auto it = consts_.find(name);
    return it == consts_.end() ? Tok::Eof : it->second.kind;
  }

  // elements must be literals (or consts) so the jit can bake them in
  ExprPtr parseLiteral(const char* what) {
    bool neg = accept(Tok::Minus);
    Token t = eat();
    if (t.kind == Tok::Ident) {
      auto it = consts_.find(t.text);
      if (it == consts_.end())
        fail(t.loc, std::string("expected a literal ") + what + " (or a const), got identifier '" + t.text + "'");
      Token c = it->second;
      c.loc = t.loc;
      t = c;
    }
    switch (t.kind) {
      case Tok::IntLit: return mk<IntLitExpr>(t.loc, neg ? -t.ival : t.ival);
      case Tok::FloatLit: return mk<FloatLitExpr>(t.loc, neg ? -t.fval : t.fval);
      case Tok::StrLit:
        if (neg) fail(t.loc, "cannot negate a string");
        return mk<StrLitExpr>(t.loc, t.text);
      case Tok::IpLit:
        if (neg) fail(t.loc, "cannot negate an IP");
        return mk<IpLitExpr>(t.loc, t.ip);
      case Tok::KwTrue:
      case Tok::KwFalse:
        if (neg) fail(t.loc, "cannot negate a bool literal");
        return mk<BoolLitExpr>(t.loc, t.kind == Tok::KwTrue);
      default:
        fail(t.loc, std::string("expected a literal ") + what + ", got " + tokName(t.kind));
    }
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
      case Tok::IpLit: eat(); return mk<IpLitExpr>(t.loc, t.ip);
      case Tok::KwTrue: eat(); return mk<BoolLitExpr>(t.loc, true);
      case Tok::KwFalse: eat(); return mk<BoolLitExpr>(t.loc, false);
      case Tok::LParen: {
        eat();
        ExprPtr e = parseExpr();
        expect(Tok::RParen, "closing ')'");
        return e;
      }
      case Tok::Ident: {
        if (t.text == "len" && peek().kind == Tok::LParen) {
          eat(); eat();
          ExprPtr arg = parseExpr();
          expect(Tok::RParen, "closing ')' of len()");
          return mk<LenExpr>(t.loc, std::move(arg));
        }
        auto it = consts_.find(t.text);
        if (it != consts_.end()) {
          eat();
          const Token& c = it->second;
          switch (c.kind) {
            case Tok::IntLit: return mk<IntLitExpr>(t.loc, c.ival);
            case Tok::FloatLit: return mk<FloatLitExpr>(t.loc, c.fval);
            case Tok::StrLit: return mk<StrLitExpr>(t.loc, c.text);
            case Tok::IpLit: return mk<IpLitExpr>(t.loc, c.ip);
            case Tok::KwTrue: return mk<BoolLitExpr>(t.loc, true);
            case Tok::KwFalse: return mk<BoolLitExpr>(t.loc, false);
            default: fail(t.loc, "bad const");
          }
        }
        eat();
        return mk<VarExpr>(t.loc, t.text); // let, param, or bare field: sema decides
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
    case ExprKind::IpLit: return ipToString(static_cast<const IpLitExpr*>(e)->addr);
    case ExprKind::Var: return static_cast<const VarExpr*>(e)->name;
    case ExprKind::Field: {
      auto* f = static_cast<const FieldExpr*>(e);
      return f->recName.empty() ? f->fieldName : f->recName + "." + f->fieldName;
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
    case ExprKind::Between: {
      auto* b = static_cast<const BetweenExpr*>(e);
      return "(" + exprToString(b->subject.get()) + " between " +
             exprToString(b->lo.get()) + " and " + exprToString(b->hi.get()) + ")";
    }
    case ExprKind::InList: {
      auto* i = static_cast<const InListExpr*>(e);
      std::string s = "(" + exprToString(i->subject.get()) + (i->negated ? " not in [" : " in [");
      for (size_t k = 0; k < i->elems.size(); k++) {
        if (k) s += ", ";
        s += exprToString(i->elems[k].get());
      }
      return s + "])";
    }
    case ExprKind::InCidr: {
      auto* i = static_cast<const InCidrExpr*>(e);
      return "(" + exprToString(i->subject.get()) + (i->negated ? " not in " : " in ") +
             ipToString(i->net) + "/" + std::to_string(i->prefix) + ")";
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
    case ExprKind::Len:
      return "len(" + exprToString(static_cast<const LenExpr*>(e)->arg.get()) + ")";
  }
  return "?";
}

} // namespace nql
