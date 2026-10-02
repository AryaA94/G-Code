#include "gcodesim/linter.hpp"
#include "helpers.hpp"

using namespace test;

namespace {

MachineConfig mill() {
  MachineConfig m;
  m.max_feed_mm_min = 3000;
  m.has_travel = true;
  m.travel_min = {-100, -100, -100};
  m.travel_max = {100, 100, 50};
  m.tools[1] = {6.0, 0.0};
  return m;
}

// Header that makes cutting legal: a tool, spindle on, feed set.
const char* kGood = "T1 M6\nS8000 M3\nF500\n";

std::vector<Diagnostic> lint_text(const std::string& body, const MachineConfig& m = mill()) {
  return lint(interpret(body, m).segments, m);
}

}  // namespace

TEST_CASE("a clean program has no lint") {
  auto d = lint_text(std::string(kGood) + "G0 X0 Y0 Z5\nG1 Z-2\nG1 X10\nG2 X20 Y0 I5 J0\nG0 Z5\nM5\nM30");
  CHECK(d.empty());
}

TEST_CASE("there are nine rules with unique codes and summaries") {
  auto rules = make_default_rules();
  REQUIRE(rules.size() == 9);
  for (std::size_t i = 0; i < rules.size(); ++i) {
    CHECK(rules[i]->code() == "LN00" + std::to_string(i + 1));
    CHECK_FALSE(rules[i]->summary().empty());
  }
}

TEST_CASE("LN001 rapid into stock") {
  auto d = lint_text(std::string(kGood) + "G0 X0 Y0 Z5\nG0 Z-1");
  REQUIRE(count_code(d, "LN001") == 1);
  CHECK(d[0].line == 5);
  CHECK(d[0].severity == Severity::Warning);
}

TEST_CASE("LN001 also catches sideways rapids below Z0") {
  auto d = lint_text(std::string(kGood) + "G0 X0 Y0 Z5\nG1 Z-1\nG0 X10");
  CHECK(count_code(d, "LN001") == 1);
}

TEST_CASE("LN001 ignores rapids going up out of the stock and drilling retracts") {
  auto d = lint_text(std::string(kGood) + "G0 X0 Y0 Z5\nG1 Z-3\nG0 Z-1\nG0 Z5\nG83 X5 Z-10 R1 Q2");
  CHECK(count_code(d, "LN001") == 0);
}

TEST_CASE("LN002 out of travel reported once per axis and side") {
  auto d = lint_text(std::string(kGood) + "G0 X150\nG0 X160\nG0 X-150\nG0 Z60");
  CHECK(count_code(d, "LN002") == 3);
  CHECK(d[0].severity == Severity::Error);
}

TEST_CASE("LN002 checks the bulge of an arc, not just its ends") {
  // both ends are at x=-60, but the arc swings out to x=-105
  auto d = lint_text(std::string(kGood) + "G0 X-60 Y-45\nG2 X-60 Y45 I0 J45");
  CHECK(count_code(d, "LN002") == 1);
}

TEST_CASE("LN002 is off when the config has no travel") {
  MachineConfig m;
  CHECK(count_code(lint_text("G0 X100000", m), "LN002") == 0);
}

TEST_CASE("LN003 missing feed is reported once") {
  auto d = lint_text("T1 M6\nS1000 M3\nG1 X1\nG1 X2\nG1 X3");
  CHECK(count_code(d, "LN003") == 1);
}

TEST_CASE("LN004 spindle off: once per stretch") {
  auto d = lint_text("T1 M6\nF100\nG1 X1\nG1 X2\nS1000 M3\nG1 X3\nM5\nG1 X4");
  CHECK(count_code(d, "LN004") == 2);
}

TEST_CASE("LN004 catches M3 with S0") {
  auto d = lint_text("T1 M6\nF100\nM3\nG1 X1");
  REQUIRE(count_code(d, "LN004") == 1);
  CHECK(d[0].message.find("S0") != std::string::npos);
}

TEST_CASE("LN005 tool change with spindle on") {
  auto d = lint_text("T1 M6\nS100 M3\nT2 M6");
  CHECK(count_code(d, "LN005") == 1);
  CHECK(count_code(lint_text("T1 M6\nS100 M3\nM5\nT2 M6"), "LN005") == 0);
}

TEST_CASE("LN006 arc radius mismatch uses Grbl's tolerance") {
  auto bad = lint_text(std::string(kGood) + "G0 X0 Y0\nG2 X10 Y0 I5.1 J0");
  CHECK(count_code(bad, "LN006") == 1);
  auto ok = lint_text(std::string(kGood) + "G0 X0 Y0\nG2 X10 Y0 I5.002 J0");
  CHECK(count_code(ok, "LN006") == 0);
}

TEST_CASE("LN007 feed above limit, once per value") {
  auto d = lint_text("T1 M6\nS100 M3\nG1 X1 F5000\nG1 X2\nG1 X3 F6000\nG1 X4 F5000");
  CHECK(count_code(d, "LN007") == 2);
}

TEST_CASE("LN008 cutting with no tool loaded, reported once") {
  auto d = lint_text("S100 M3\nF100\nG1 X1\nG1 X2");
  CHECK(count_code(d, "LN008") == 1);
  CHECK(count_code(lint_text("T1\nS100 M3\nF100\nG1 X1"), "LN008") == 1);  // T without M6 isn't loaded
}

TEST_CASE("LN009 plunge deeper than the tool diameter") {
  auto d = lint_text(std::string(kGood) + "G0 X0 Y0 Z5\nG1 Z-7");
  REQUIRE(count_code(d, "LN009") == 1);
  CHECK(d[0].message.find("7.000 mm") != std::string::npos);
}

TEST_CASE("LN009 counts only the depth below Z0") {
  // 11 mm of travel, but only 5 mm of it is in the material
  CHECK(count_code(lint_text(std::string(kGood) + "G0 X0 Y0 Z6\nG1 Z-5"), "LN009") == 0);
  // two passes of 4 mm each are fine; the second starts at -4
  CHECK(count_code(lint_text(std::string(kGood) + "G0 Z1\nG1 Z-4\nG1 X5\nG1 Z-8"), "LN009") == 0);
}

TEST_CASE("LN009 skips drilling cycles and tools that aren't in the config") {
  CHECK(count_code(lint_text(std::string(kGood) + "G0 Z5\nG81 X0 Z-30 R1"), "LN009") == 0);
  CHECK(count_code(lint_text("T9 M6\nS1 M3\nF100\nG1 Z-50"), "LN009") == 0);
}

TEST_CASE("LN009 applies to ramps and helixes, not just vertical plunges") {
  auto d = lint_text(std::string(kGood) + "G0 X5 Y0 Z0\nG3 X5 Y0 Z-8 I-5 J0");
  CHECK(count_code(d, "LN009") == 1);
}

TEST_CASE("lint output is sorted by line") {
  auto d = lint_text("G1 X1\nG0 Z-5\nG0 X9999");
  for (std::size_t i = 1; i < d.size(); ++i) CHECK(d[i - 1].line <= d[i].line);
}
