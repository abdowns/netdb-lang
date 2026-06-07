#pragma once
#include <string_view>

#include "ast.hpp"

namespace nql {

Program parse(std::string_view src);

} // namespace nql
