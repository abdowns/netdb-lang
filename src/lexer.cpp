#include "lexer.hpp"

#include <cctype>
#include <unordered_map>

namespace nql {
namespace {

const std::unordered_map<std::string_view, Tok> kKeywords = {
    {"schema", Tok::KwSchema},   {"filter", Tok::KwFilter},
    {"query", Tok::KwQuery},     {"const", Tok::KwConst},
    {"let", Tok::KwLet},         {"over", Tok::KwOver},
    {"where", Tok::KwWhere},     {"select", Tok::KwSelect},
    {"order", Tok::KwOrder},     {"by", Tok::KwBy},
    {"asc", Tok::KwAsc},         {"desc", Tok::KwDesc},
    {"limit", Tok::KwLimit},     {"and", Tok::KwAnd},
    {"or", Tok::KwOr},           {"not", Tok::KwNot},
    {"in", Tok::KwIn},           {"contains", Tok::KwContains},
    {"startswith", Tok::KwStartswith},
    {"endswith", Tok::KwEndswith},
    {"matches", Tok::KwMatches},
    {"true", Tok::KwTrue},       {"false", Tok::KwFalse},
};

class Lexer {
public:
  explicit Lexer(std::string_view src) : src_(src) {}

  std::vector<Token> run() {
    std::vector<Token> out;
    for (;;) {
      skipTrivia();
      Token t = next();
      out.push_back(t);
      if (t.kind == Tok::Eof) break;
    }
    return out;
  }

private:
  std::string_view src_;
  size_t pos_ = 0;
  uint32_t line_ = 1, col_ = 1;

  bool atEnd() const { return pos_ >= src_.size(); }
  char peek(size_t off = 0) const {
    return pos_ + off < src_.size() ? src_[pos_ + off] : '\0';
  }
  char advance() {
    char c = src_[pos_++];
    if (c == '\n') { line_++; col_ = 1; } else { col_++; }
    return c;
  }
  SrcLoc here() const { return {line_, col_}; }

  void skipTrivia() {
    for (;;) {
      char c = peek();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        advance();
      } else if (c == '#' || (c == '/' && peek(1) == '/')) {
        while (!atEnd() && peek() != '\n') advance();
      } else {
        return;
      }
    }
  }

  Token make(Tok k) {
    Token t;
    t.kind = k;
    t.loc = here();
    return t;
  }

  Token next() {
    if (atEnd()) return make(Tok::Eof);
    SrcLoc loc = here();
    char c = peek();

    if (std::isalpha((unsigned char)c) || c == '_') return identOrKeyword(loc);
    if (std::isdigit((unsigned char)c)) return number(loc);
    if (c == '"') return stringLit(loc);

    advance();
    Token t;
    t.loc = loc;
    switch (c) {
      case '{': t.kind = Tok::LBrace; return t;
      case '}': t.kind = Tok::RBrace; return t;
      case '(': t.kind = Tok::LParen; return t;
      case ')': t.kind = Tok::RParen; return t;
      case '[': t.kind = Tok::LBracket; return t;
      case ']': t.kind = Tok::RBracket; return t;
      case ',': t.kind = Tok::Comma; return t;
      case ';': t.kind = Tok::Semi; return t;
      case ':': t.kind = Tok::Colon; return t;
      case '.': t.kind = Tok::Dot; return t;
      case '+': t.kind = Tok::Plus; return t;
      case '*': t.kind = Tok::Star; return t;
      case '/': t.kind = Tok::Slash; return t;
      case '%': t.kind = Tok::Percent; return t;
      case '&': t.kind = Tok::Amp; return t;
      case '|': t.kind = Tok::Pipe; return t;
      case '^': t.kind = Tok::Caret; return t;
      case '~': t.kind = Tok::Tilde; return t;
      case '-':
        if (peek() == '>') { advance(); t.kind = Tok::Arrow; return t; }
        t.kind = Tok::Minus; return t;
      case '=':
        if (peek() == '=') { advance(); t.kind = Tok::Eq; return t; }
        t.kind = Tok::Assign; return t;
      case '!':
        if (peek() == '=') { advance(); t.kind = Tok::Ne; return t; }
        fail(loc, "unexpected '!' (use 'not' for negation)");
      case '<':
        if (peek() == '=') { advance(); t.kind = Tok::Le; return t; }
        if (peek() == '<') { advance(); t.kind = Tok::Shl; return t; }
        t.kind = Tok::Lt; return t;
      case '>':
        if (peek() == '=') { advance(); t.kind = Tok::Ge; return t; }
        if (peek() == '>') { advance(); t.kind = Tok::Shr; return t; }
        t.kind = Tok::Gt; return t;
      default:
        fail(loc, std::string("unexpected character '") + c + "'");
    }
  }

  Token identOrKeyword(SrcLoc loc) {
    size_t start = pos_;
    while (!atEnd() && (std::isalnum((unsigned char)peek()) || peek() == '_')) advance();
    std::string_view text = src_.substr(start, pos_ - start);
    Token t;
    t.loc = loc;
    auto it = kKeywords.find(text);
    if (it != kKeywords.end()) {
      t.kind = it->second;
    } else {
      t.kind = Tok::Ident;
      t.text = std::string(text);
    }
    return t;
  }

  bool tryIp(Token& t) {
    size_t save = pos_;
    uint32_t saveLine = line_, saveCol = col_;
    uint32_t octets[4];
    for (int i = 0; i < 4; i++) {
      if (!std::isdigit((unsigned char)peek())) goto nope;
      {
        uint32_t v = 0;
        int digits = 0;
        while (std::isdigit((unsigned char)peek()) && digits < 4) {
          v = v * 10 + (advance() - '0');
          digits++;
        }
        if (digits > 3 || v > 255) goto nope;
        octets[i] = v;
      }
      if (i < 3) {
        if (peek() != '.') goto nope;
        advance();
      }
    }
    if (peek() == '.' || std::isalnum((unsigned char)peek()) || peek() == '_') goto nope;
    t.kind = Tok::IpLit;
    t.ip = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
    return true;
  nope:
    pos_ = save;
    line_ = saveLine;
    col_ = saveCol;
    return false;
  }

  Token number(SrcLoc loc) {
    Token t;
    t.loc = loc;
    if (tryIp(t)) return t;

    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
      advance(); advance();
      uint64_t v = 0;
      bool any = false;
      while (std::isxdigit((unsigned char)peek()) || peek() == '_') {
        char c = advance();
        if (c == '_') continue;
        any = true;
        v = v * 16 + (std::isdigit((unsigned char)c) ? c - '0'
                                                     : (std::tolower(c) - 'a') + 10);
      }
      if (!any) fail(loc, "malformed hex literal");
      t.kind = Tok::IntLit;
      t.ival = (int64_t)v;
      return t;
    }

    uint64_t v = 0;
    while (std::isdigit((unsigned char)peek()) || peek() == '_') {
      char c = advance();
      if (c == '_') continue;
      v = v * 10 + (c - '0');
    }

    if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
      advance();
      double frac = 0, scale = 0.1;
      while (std::isdigit((unsigned char)peek())) {
        frac += (advance() - '0') * scale;
        scale *= 0.1;
      }
      t.kind = Tok::FloatLit;
      t.fval = (double)v + frac;
      return t;
    }

    if (std::isalpha((unsigned char)peek())) fail(loc, "unexpected character after number");

    t.kind = Tok::IntLit;
    t.ival = (int64_t)v;
    return t;
  }

  Token stringLit(SrcLoc loc) {
    advance();
    std::string out;
    for (;;) {
      if (atEnd()) fail(loc, "unterminated string literal");
      char c = advance();
      if (c == '"') break;
      if (c == '\n') fail(loc, "newline in string literal");
      if (c == '\\') {
        if (atEnd()) fail(loc, "unterminated escape");
        char e = advance();
        switch (e) {
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '0': out += '\0'; break;
          default: fail(loc, std::string("unknown escape '\\") + e + "'");
        }
      } else {
        out += c;
      }
    }
    Token t;
    t.kind = Tok::StrLit;
    t.loc = loc;
    t.text = std::move(out);
    return t;
  }
};

} // namespace

std::vector<Token> lex(std::string_view src) { return Lexer(src).run(); }

} // namespace nql
