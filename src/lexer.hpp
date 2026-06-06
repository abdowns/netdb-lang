#pragma once
#include <string_view>
#include <vector>

#include "token.hpp"

namespace nql {

std::vector<Token> lex(std::string_view src);

} // namespace nql
