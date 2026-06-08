#pragma once
#include <cstdint>
#include <string>

#include "diag.hpp"

namespace nql {

enum class Tok : uint8_t {
  Eof,
  Ident,
  IntLit,
  FloatLit,
  StrLit,
  IpLit,

  KwSchema, KwFilter, KwQuery, KwConst, KwLet, KwOver,
  KwWhere, KwSelect, KwOrder, KwBy, KwAsc, KwDesc, KwLimit,
  KwAnd, KwOr, KwNot, KwIn, KwBetween,
  KwContains, KwStartswith, KwEndswith, KwMatches,
  KwTrue, KwFalse,

  LBrace, RBrace, LParen, RParen, LBracket, RBracket,
  Comma, Semi, Colon, Dot, Arrow,
  Eq, Ne, Lt, Le, Gt, Ge,
  Plus, Minus, Star, Slash, Percent,
  Amp, Pipe, Caret, Tilde, Shl, Shr,
  Assign,
};

struct Token {
  Tok kind = Tok::Eof;
  SrcLoc loc;
  std::string text;
  int64_t ival = 0;
  double fval = 0;
  uint32_t ip = 0; // host byte order
};

inline const char* tokName(Tok t) {
  switch (t) {
    case Tok::Eof: return "end of file";
    case Tok::Ident: return "identifier";
    case Tok::IntLit: return "integer literal";
    case Tok::FloatLit: return "float literal";
    case Tok::StrLit: return "string literal";
    case Tok::IpLit: return "IP literal";
    case Tok::KwSchema: return "'schema'";
    case Tok::KwFilter: return "'filter'";
    case Tok::KwQuery: return "'query'";
    case Tok::KwConst: return "'const'";
    case Tok::KwLet: return "'let'";
    case Tok::KwOver: return "'over'";
    case Tok::KwWhere: return "'where'";
    case Tok::KwSelect: return "'select'";
    case Tok::KwOrder: return "'order'";
    case Tok::KwBy: return "'by'";
    case Tok::KwAsc: return "'asc'";
    case Tok::KwDesc: return "'desc'";
    case Tok::KwLimit: return "'limit'";
    case Tok::KwAnd: return "'and'";
    case Tok::KwOr: return "'or'";
    case Tok::KwNot: return "'not'";
    case Tok::KwIn: return "'in'";
    case Tok::KwBetween: return "'between'";
    case Tok::KwContains: return "'contains'";
    case Tok::KwStartswith: return "'startswith'";
    case Tok::KwEndswith: return "'endswith'";
    case Tok::KwMatches: return "'matches'";
    case Tok::KwTrue: return "'true'";
    case Tok::KwFalse: return "'false'";
    case Tok::LBrace: return "'{'";
    case Tok::RBrace: return "'}'";
    case Tok::LParen: return "'('";
    case Tok::RParen: return "')'";
    case Tok::LBracket: return "'['";
    case Tok::RBracket: return "']'";
    case Tok::Comma: return "','";
    case Tok::Semi: return "';'";
    case Tok::Colon: return "':'";
    case Tok::Dot: return "'.'";
    case Tok::Arrow: return "'->'";
    case Tok::Eq: return "'=='";
    case Tok::Ne: return "'!='";
    case Tok::Lt: return "'<'";
    case Tok::Le: return "'<='";
    case Tok::Gt: return "'>'";
    case Tok::Ge: return "'>='";
    case Tok::Plus: return "'+'";
    case Tok::Minus: return "'-'";
    case Tok::Star: return "'*'";
    case Tok::Slash: return "'/'";
    case Tok::Percent: return "'%'";
    case Tok::Amp: return "'&'";
    case Tok::Pipe: return "'|'";
    case Tok::Caret: return "'^'";
    case Tok::Tilde: return "'~'";
    case Tok::Shl: return "'<<'";
    case Tok::Shr: return "'>>'";
    case Tok::Assign: return "'='";
  }
  return "?";
}

} // namespace nql
