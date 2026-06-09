#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "diag.hpp"
#include "reflect.hpp"
#include "types.hpp"

namespace nql {

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

enum class ExprKind : uint8_t {
  IntLit, FloatLit, StrLit, BoolLit, IpLit,
  Field,
  Unary, Binary, InList, InCidr, StrOp,
};

enum class UnOp : uint8_t { Not, Neg, BitNot };

enum class BinOp : uint8_t {
  Or, And,
  Eq, Ne, Lt, Le, Gt, Ge,
  Add, Sub, Mul, Div, Mod,
  BitAnd, BitOr, BitXor, Shl, Shr,
};

enum class StrOpKind : uint8_t { Contains, StartsWith, EndsWith, Matches };

struct Expr {
  ExprKind kind;
  SrcLoc loc;
  Ty type = Ty::Invalid;

  explicit Expr(ExprKind k) : kind(k) {}
  virtual ~Expr() = default;
};

struct IntLitExpr : Expr {
  int64_t v;
  IntLitExpr(int64_t v) : Expr(ExprKind::IntLit), v(v) {}
};

struct FloatLitExpr : Expr {
  double v;
  FloatLitExpr(double v) : Expr(ExprKind::FloatLit), v(v) {}
};

struct StrLitExpr : Expr {
  std::string v;
  StrLitExpr(std::string v) : Expr(ExprKind::StrLit), v(std::move(v)) {}
};

struct BoolLitExpr : Expr {
  bool v;
  BoolLitExpr(bool v) : Expr(ExprKind::BoolLit), v(v) {}
};

struct IpLitExpr : Expr {
  uint32_t addr; // host byte order, not network
  IpLitExpr(uint32_t a) : Expr(ExprKind::IpLit), addr(a) {}
};

struct FieldExpr : Expr {
  std::string recName;
  std::string fieldName;
  const FieldInfo* fi = nullptr;
  FieldExpr(std::string r, std::string f)
      : Expr(ExprKind::Field), recName(std::move(r)), fieldName(std::move(f)) {}
};

struct UnaryExpr : Expr {
  UnOp op;
  ExprPtr operand;
  UnaryExpr(UnOp op, ExprPtr e) : Expr(ExprKind::Unary), op(op), operand(std::move(e)) {}
};

struct BinaryExpr : Expr {
  BinOp op;
  ExprPtr lhs, rhs;
  BinaryExpr(BinOp op, ExprPtr l, ExprPtr r)
      : Expr(ExprKind::Binary), op(op), lhs(std::move(l)), rhs(std::move(r)) {}
};

struct InListExpr : Expr {
  ExprPtr subject;
  std::vector<ExprPtr> elems;
  bool negated = false;
  InListExpr(ExprPtr s) : Expr(ExprKind::InList), subject(std::move(s)) {}
};

struct InCidrExpr : Expr {
  ExprPtr subject;
  uint32_t net;
  int prefix;
  bool negated = false;
  InCidrExpr(ExprPtr s, uint32_t net, int prefix)
      : Expr(ExprKind::InCidr), subject(std::move(s)), net(net), prefix(prefix) {}

  uint32_t mask() const { return prefix == 0 ? 0u : ~0u << (32 - prefix); }
};

struct StrOpExpr : Expr {
  StrOpKind op;
  ExprPtr subject, pattern;
  StrOpExpr(StrOpKind op, ExprPtr s, ExprPtr p)
      : Expr(ExprKind::StrOp), op(op), subject(std::move(s)), pattern(std::move(p)) {}
};

struct FilterDecl {
  std::string name;
  std::string paramName;
  std::string schemaName;
  const Schema* schema = nullptr;
  ExprPtr body;
  SrcLoc loc;
};

struct Program {
  std::vector<std::unique_ptr<Schema>> schemas; // unique_ptr: Schema* stays stable on growth
  std::vector<FilterDecl> filters;

  const Schema* findSchema(const std::string& n) const {
    for (const auto& s : schemas)
      if (s->name == n) return s.get();
    return nullptr;
  }
};

std::string exprToString(const Expr* e);

} // namespace nql
