#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "diag.hpp"
#include "reflect.hpp"

namespace nql {

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

enum class ExprKind : uint8_t {
  IntLit, FloatLit, StrLit, BoolLit,
  Field,
  Unary, Binary, StrOp,
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

struct FieldExpr : Expr {
  std::string recName;
  std::string fieldName;
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
  ExprPtr body;
  SrcLoc loc;
};

struct Program {
  std::vector<Schema> schemas;
  std::vector<FilterDecl> filters;

  const Schema* findSchema(const std::string& n) const {
    for (const auto& s : schemas)
      if (s.name == n) return &s;
    return nullptr;
  }
};

std::string dumpExpr(const Expr* e, int indent = 0);

} // namespace nql
