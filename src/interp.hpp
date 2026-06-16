#pragma once
#include "ast.hpp"

namespace nql {

// tree walking evaluator, used to cross check the jit and as a bench baseline
bool evalPredicate(const Expr* body, const std::vector<LetStmt>& lets, const uint8_t* rec);

inline bool evalFilter(const FilterDecl& f, const uint8_t* rec) {
  return evalPredicate(f.body.get(), f.lets, rec);
}

} // namespace nql
