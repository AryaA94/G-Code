#include "gcodesim/lexer.hpp"
#include "gcodesim/parser.hpp"
#include "helpers.hpp"

using namespace test;

namespace {
LexedLine lex(std::string_view s, std::vector<Diagnostic>* diags = nullptr) {
  std::vector<Diagnostic> local;
  return lex_line(s, 1, diags ? *diags : local);
}
}  // namespace

TEST_CASE("lexer splits a line into letter/number words") {
  auto l = lex("G1 X10 Y-2.5 F200");
  REQUIRE(l.words.size() == 4);
  CHECK(l.words[0].letter == 'G');
  CHECK(l.words[0].value == 1.0);
  CHECK(l.words[2].value == -2.5);
  CHECK(l.words[3].column == 14);
}

TEST_CASE("lexer accepts lower case, no spaces and spaces inside a word") {
  auto l = lex("g1x10y.5z-.25 F 300");
  REQUIRE(l.words.size() == 5);
  CHECK(l.words[0].letter == 'G');
  CHECK(l.words[2].value == 0.5);
  CHECK(l.words[3].value == -0.25);
  CHECK(l.words[4].value == 300.0);
}

TEST_CASE("lexer handles '5.' and '+5' number forms") {
  auto l = lex("X5. Y+3");
  REQUIRE(l.words.size() == 2);
  CHECK(l.words[0].value == 5.0);
  CHECK(l.words[1].value == 3.0);
}

TEST_CASE("lexer drops both comment styles") {
  auto l = lex("G0 (move up) Z5 ; and a trailing note X99");
  REQUIRE(l.words.size() == 2);
  CHECK(l.words[1].letter == 'Z');
}

TEST_CASE("lexer: unclosed comment is GC001") {
  std::vector<Diagnostic> d;
  auto l = lex("G0 X1 (oops", &d);
  CHECK(l.words.size() == 2);
  REQUIRE(d.size() == 1);
  CHECK(d[0].code == "GC001");
  CHECK(d[0].column == 7);
}

TEST_CASE("lexer: letter without a number is GC002") {
  std::vector<Diagnostic> d;
  auto l = lex("G1 X Y2", &d);
  CHECK(has_code(d, "GC002"));
  REQUIRE(l.words.size() == 2);
  CHECK(l.words[1].letter == 'Y');
}

TEST_CASE("lexer: stray characters are GC003 and skipped") {
  std::vector<Diagnostic> d;
  auto l = lex("G1 X1 $ Y2", &d);
  CHECK(has_code(d, "GC003"));
  CHECK(l.words.size() == 3);
}

TEST_CASE("lexer: parameters are GC004 and stop the line") {
  std::vector<Diagnostic> d;
  auto l = lex("G1 X#1 Y2", &d);
  CHECK(has_code(d, "GC004"));
  CHECK(l.words.size() == 1);
}

TEST_CASE("lexer: huge numbers are GC005") {
  std::vector<Diagnostic> d;
  std::string big = "X1" + std::string(400, '0');
  lex(big, &d);
  CHECK(has_code(d, "GC005"));
}

TEST_CASE("lexer: block delete and percent sign") {
  auto l = lex("/G0 X1");
  CHECK(l.block_delete);
  CHECK(l.words.size() == 2);
  CHECK(lex("%").words.empty());
}

TEST_CASE("parser stores G codes in tenths") {
  std::vector<Diagnostic> d;
  auto blocks = parse_program("G91.1 G1 X1", d);
  REQUIRE(blocks.size() == 1);
  CHECK(blocks[0].has_g(911));
  CHECK(blocks[0].has_g(10));
  CHECK(d.empty());
}

TEST_CASE("parser: repeated letter is GC006") {
  std::vector<Diagnostic> d;
  auto blocks = parse_program("G1 X1 X2", d);
  CHECK(has_code(d, "GC006"));
  CHECK(*blocks[0].get('X') == 1.0);
}

TEST_CASE("parser: two motion codes on a line is GC008") {
  std::vector<Diagnostic> d;
  parse_program("G0 G1 X1", d);
  CHECK(has_code(d, "GC008"));
}

TEST_CASE("parser: two spindle M codes on a line is GC008") {
  std::vector<Diagnostic> d;
  parse_program("M3 M5", d);
  CHECK(has_code(d, "GC008"));
}

TEST_CASE("parser: unknown G code is GC009, unsupported is GC010") {
  std::vector<Diagnostic> d;
  parse_program("G12 X1\nG41 D1\nG1.55", d);
  CHECK(count_code(d, "GC009") == 1);
  CHECK(count_code(d, "GC010") == 1);
  CHECK(count_code(d, "GC007") == 1);
}

TEST_CASE("parser: G40 is accepted as a no-op") {
  std::vector<Diagnostic> d;
  parse_program("G40", d);
  CHECK(d.empty());
}

TEST_CASE("parser: unknown M code is a GC011 warning") {
  std::vector<Diagnostic> d;
  parse_program("M98 P100", d);
  REQUIRE(d.size() == 1);
  CHECK(d[0].code == "GC011");
  CHECK(d[0].severity == Severity::Warning);
}

TEST_CASE("parser: rotary axes are GC012") {
  std::vector<Diagnostic> d;
  parse_program("G1 A90", d);
  CHECK(has_code(d, "GC012"));
}

TEST_CASE("parser drops N and O words and empty lines") {
  std::vector<Diagnostic> d;
  auto blocks = parse_program("O1000\nN10 G0 X1\n\n(comment only)\n", d);
  REQUIRE(blocks.size() == 1);
  CHECK(blocks[0].line == 2);
  CHECK(d.empty());
}

TEST_CASE("parser counts lines with Windows line endings") {
  std::vector<Diagnostic> d;
  auto blocks = parse_program("G0 X1\r\nG0 X2\r\n", d);
  REQUIRE(blocks.size() == 2);
  CHECK(blocks[1].line == 2);
  CHECK(d.empty());
}
