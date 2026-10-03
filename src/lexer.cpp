#include "gcodesim/lexer.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>

namespace gcodesim {

namespace {

// Parse a plain decimal ("12", "-0.5"). std::from_chars for double is the
// fast path; Apple's libc++ before LLVM 17 doesn't have it, so fall back to
// a stream fixed to the "C" locale (so ',' is never taken as the point).
bool parse_double(const std::string& s, double& value) {
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 170000
  std::istringstream in(s);
  in.imbue(std::locale::classic());
  in >> value;
  return !in.fail() && in.peek() == std::char_traits<char>::eof() && std::isfinite(value);
#else
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  return ec == std::errc() && ptr == s.data() + s.size() && std::isfinite(value);
#endif
}

}  // namespace

namespace {

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

}  // namespace

LexedLine lex_line(std::string_view text, int line_no, std::vector<Diagnostic>& diags) {
  LexedLine out;
  out.line = line_no;

  auto error = [&](std::string code, std::size_t pos, std::string msg) {
    diags.push_back({Severity::Error, std::move(code), line_no, static_cast<int>(pos) + 1, std::move(msg)});
  };

  std::size_t i = 0;
  const std::size_t n = text.size();
  while (i < n && is_space(text[i])) ++i;
  if (i < n && text[i] == '/') {
    out.block_delete = true;
    ++i;
  }

  while (i < n) {
    char c = text[i];
    if (is_space(c)) {
      ++i;
      continue;
    }
    if (c == ';') break;
    if (c == '%') {  // program start/end marker used by many controllers
      ++i;
      continue;
    }
    if (c == '(') {
      std::size_t close = text.find(')', i);
      if (close == std::string_view::npos) {
        error("GC001", i, "comment is never closed (missing ')')");
        break;
      }
      i = close + 1;
      continue;
    }
    if (c == '#' || c == '[') {
      error("GC004", i, "parameters and expressions ('#', '[') are not supported");
      break;  // the rest of the line depends on them, so stop here
    }
    if (!std::isalpha(static_cast<unsigned char>(c))) {
      error("GC003", i, std::string("unexpected character '") + c + "'");
      ++i;
      continue;
    }

    const std::size_t letter_pos = i;
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    ++i;
    while (i < n && is_space(text[i])) ++i;

    // number: [+-] digits [. digits]  (".5" and "5." are both fine)
    std::size_t start = i;
    if (i < n && (text[i] == '+' || text[i] == '-')) ++i;
    std::size_t digits = 0;
    while (i < n && is_digit(text[i])) ++i, ++digits;
    if (i < n && text[i] == '.') {
      ++i;
      while (i < n && is_digit(text[i])) ++i, ++digits;
    }
    if (digits == 0) {
      error("GC002", letter_pos, std::string("'") + letter + "' has no number after it");
      continue;
    }

    // from_chars doesn't accept a leading '+'
    std::size_t num_begin = text[start] == '+' ? start + 1 : start;
    std::string number(text.substr(num_begin, i - num_begin));
    if (number.back() == '.') number.pop_back();
    if (!number.empty() && number.front() == '.') number.insert(0, "0");
    if (number.size() > 1 && number[0] == '-' && number[1] == '.') number.insert(1, "0");

    double value = 0.0;
    if (!parse_double(number, value)) {
      error("GC005", letter_pos, "number '" + number + "' is out of range");
      continue;
    }
    out.words.push_back({letter, value, static_cast<int>(letter_pos) + 1});
  }
  return out;
}

}  // namespace gcodesim
