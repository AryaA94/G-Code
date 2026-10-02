#include <cmath>
#include <numbers>

#include "gcodesim/report.hpp"
#include "gcodesim/svg.hpp"
#include "helpers.hpp"

using namespace test;

TEST_CASE("format_duration") {
  CHECK(format_duration(0) == "0.0 s");
  CHECK(format_duration(12.44) == "12.4 s");
  CHECK(format_duration(59.96) == "1 min 00.0 s");
  CHECK(format_duration(187) == "3 min 07.0 s");
  CHECK(format_duration(3725.3) == "1 h 02 min 05.3 s");
}

TEST_CASE("round_sig keeps 6 significant digits and no negative zero") {
  CHECK(round_sig(3.14159265) == 3.14159);
  CHECK(round_sig(123456789.0) == 123457000.0);
  CHECK(round_sig(-1e-20) == -1e-20);
  CHECK(std::signbit(round_sig(-0.0)) == false);
}

TEST_CASE("format_diagnostic matches the compiler-style layout") {
  Diagnostic d{Severity::Warning, "LN001", 5, 1, "rapid"};
  CHECK(format_diagnostic(d, "a.nc") == "a.nc:5:1: warning[LN001]: rapid");
}

TEST_CASE("stats count moves, lengths and tools") {
  MachineConfig m;
  auto a = analyze("T1 M6\nS1000 M3\nG0 X10\nG1 X20 F600\nG2 X30 Y0 R5\nT2 M6\nG1 Y5", m);
  const Stats& s = a.stats;
  CHECK(s.rapid_moves == 1);
  CHECK(s.linear_moves == 2);
  CHECK(s.arc_moves == 1);
  CHECK(s.tool_changes == 2);
  CHECK(s.tools == std::vector<int>{1, 2});
  CHECK(s.rapid_length_mm == Approx(10));
  CHECK(s.cut_length_mm == Approx(10 + 5 * std::numbers::pi + 5));
  CHECK(s.cut_bounds.min.x == Approx(10));
  CHECK(s.cut_bounds.max.y == Approx(5));
  CHECK(s.total_time_s == Approx(s.cut_time_s + s.rapid_time_s + s.other_time_s));
  CHECK(s.max_feed_mm_min == 600);
}

TEST_CASE("stats_json has the documented keys") {
  MachineConfig m;
  auto j = stats_json(analyze("G1 X1 F100", m));
  CHECK(j.contains("time_s"));
  CHECK(j["time_s"].contains("total"));
  CHECK(j["moves"]["linear"] == 1);
  CHECK(j["cut_bounds"]["max"][0] == 1.0);
}

TEST_CASE("diagnostics_json counts errors and warnings") {
  std::vector<Diagnostic> d = {{Severity::Error, "LN003", 2, 1, "x"},
                               {Severity::Warning, "LN001", 3, 1, "y"}};
  auto j = diagnostics_json(d);
  CHECK(j["errors"] == 1);
  CHECK(j["warnings"] == 1);
  CHECK(j["diagnostics"][0]["code"] == "LN003");
}

TEST_CASE("stats_text mentions cycle time and tools") {
  MachineConfig m;
  auto a = analyze("T3 M6\nG1 X1 F100", m);
  std::string t = stats_text(a, m);
  CHECK(t.find("Cycle time") != std::string::npos);
  CHECK(t.find("T3") != std::string::npos);
}

TEST_CASE("toolpath_json has one entry per segment with points and times") {
  MachineConfig m;
  auto a = analyze("G0 X1\nG2 X1 Y0 I1 J0 F100\nG4 P1", m);
  auto j = toolpath_json(a);
  REQUIRE(j["moves"].size() == 3);
  CHECK(j["moves"][1]["type"] == "arc");
  CHECK(j["moves"][1]["points"].size() > 10);
  CHECK(j["moves"][2]["dt"] == 1.0);
  CHECK_FALSE(j["profile"].empty());
}

TEST_CASE("svg is a complete document with one polyline per cutting move") {
  MachineConfig m;
  auto p = interpret("G0 X0 Y0\nG1 X10 F100\nG1 Y10 F200\nG0 X0", m);
  std::string svg = render_svg(p.segments);
  CHECK(svg.rfind("<svg", 0) == 0);
  CHECK(svg.find("</svg>") != std::string::npos);
  CHECK(svg.find("stroke-dasharray") != std::string::npos);
  std::size_t lines = 0;
  for (std::size_t pos = svg.find("<polyline stroke="); pos != std::string::npos;
       pos = svg.find("<polyline stroke=", pos + 1))
    ++lines;
  CHECK(lines == 2);
}

TEST_CASE("svg without rapids, by depth, and with nothing to draw") {
  MachineConfig m;
  auto p = interpret("G0 X5\nG1 Z-1 F100", m);
  SvgOptions o;
  o.show_rapids = false;
  o.color_by = ColorBy::Depth;
  o.title = "a <b> & c";
  std::string svg = render_svg(p.segments, o);
  CHECK(svg.find("depth") != std::string::npos);
  CHECK(svg.find("a &lt;b&gt; &amp; c") != std::string::npos);
  CHECK(render_svg({}).find("no moves") != std::string::npos);
}

TEST_CASE("color_ramp gives hex colours and clamps") {
  CHECK(color_ramp(0).size() == 7);
  CHECK(color_ramp(-5) == color_ramp(0));
  CHECK(color_ramp(5) == color_ramp(1));
  CHECK(color_ramp(0) != color_ramp(1));
}
