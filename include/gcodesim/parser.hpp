#pragma once

#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "gcodesim/diagnostics.hpp"
#include "gcodesim/lexer.hpp"

namespace gcodesim {

// One line of G-code after checking which words belong together.
// G and M codes can repeat on a line; every other letter appears at most once.
struct Block {
  int line = 0;
  bool block_delete = false;
  std::vector<int> g_codes;  // in tenths so G38.2 fits: G1 -> 10, G91.1 -> 911
  std::vector<int> m_codes;
  std::array<std::optional<double>, 26> values{};  // indexed by letter - 'A'
  std::array<int, 26> columns{};                   // where each value's letter was

  const std::optional<double>& get(char letter) const { return values[letter - 'A']; }
  bool has(char letter) const { return values[letter - 'A'].has_value(); }
  int column(char letter) const { return columns[letter - 'A']; }
  bool has_g(int tenths) const;
  bool has_m(int code) const;
  bool empty() const;
};

// Checks one lexed line: repeated letters, unknown or unsupported codes,
// two codes from the same modal group. Bad words are dropped with a
// diagnostic, the rest of the block is kept.
Block parse_block(const LexedLine& lexed, std::vector<Diagnostic>& diags);

// Lex + parse a whole program, one Block per non-empty line.
std::vector<Block> parse_program(std::string_view text, std::vector<Diagnostic>& diags);

// Which modal group a G code belongs to (RS-274/NGC table), -1 if unknown.
int g_modal_group(int tenths);

// G41/G42 and friends: codes we recognise but deliberately do not simulate.
bool is_unsupported_g(int tenths);

}  // namespace gcodesim
