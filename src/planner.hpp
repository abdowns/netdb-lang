#pragma once
#include <string>

#include "ast.hpp"

namespace nql {

// constant folds and reorders and chains cheapest first; runs after sema
void plan(Program& prog);

std::string explainPlan(const Program& prog);

int exprCost(const Expr* e);

} // namespace nql
