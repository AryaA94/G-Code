#pragma once

#include <string_view>
#include <vector>

#include "gcodesim/diagnostics.hpp"

namespace gcodesim {

// A letter and its number, e.g. "X-12.5". Column points at the letter.
struct Word {
  char letter = 0;  // always upper case
  double value = 0.0;
  int column = 0;
};

struct LexedLine {
  int line = 0;
  std::vector<Word> words;
  bool block_delete = false;  // line started with '/'
};

// Splits one line of G-code into words. Comments "( ... )" and "; ..." are
// dropped, spaces between a letter and its number are allowed ("G 1").
// Problems go into `diags`; whatever could be read is still returned.
LexedLine lex_line(std::string_view text, int line_no, std::vector<Diagnostic>& diags);

}  // namespace gcodesim
