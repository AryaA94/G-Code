#include "gcodesim/machine.hpp"
#include "helpers.hpp"

using namespace test;

TEST_CASE("absolute moves go to the given coordinates") {
  auto m = moves(run("G90 G0 X10 Y20 Z5\nG1 X15 F100"));
  REQUIRE(m.size() == 2);
  check_point(m[0].end, 10, 20, 5);
  check_point(m[1].start, 10, 20, 5);
  check_point(m[1].end, 15, 20, 5);
  CHECK(m[0].type == MoveType::Rapid);
  CHECK(m[1].type == MoveType::Linear);
}

TEST_CASE("G91 incremental moves add to the current position") {
  auto m = moves(run("G0 X10 Y10\nG91\nG1 X5 Y-2 F100\nX5"));
  check_point(m[1].end, 15, 8, 0);
  check_point(m[2].end, 20, 8, 0);
}

TEST_CASE("motion mode is modal: a line of only coordinates repeats it") {
  auto m = moves(run("G1 X1 F100\nX2\nY3"));
  REQUIRE(m.size() == 3);
  for (const auto& s : m) CHECK(s.type == MoveType::Linear);
}

TEST_CASE("G20 converts inches, including the feed rate") {
  auto m = moves(run("G20 G1 X1 F10"));
  check_point(m[0].end, 25.4, 0, 0);
  CHECK(m[0].feed == Approx(254.0));
}

TEST_CASE("switching back to G21 keeps positions but reads new words in mm") {
  auto m = moves(run("G20 G0 X1\nG21 G0 Y10"));
  check_point(m[1].end, 25.4, 10, 0);
}

TEST_CASE("feed and spindle state are copied onto every move") {
  auto m = moves(run("S5000 M3\nG1 X1 F200\nM5\nG1 X2"));
  CHECK(m[0].spindle_on);
  CHECK(m[0].spindle_rpm == 5000.0);
  CHECK(m[0].feed == 200.0);
  CHECK_FALSE(m[1].spindle_on);
  CHECK(m[1].feed == 200.0);
}

TEST_CASE("work offsets shift machine coordinates, not work coordinates") {
  MachineConfig mc;
  mc.work_offsets[0] = {100, 50, -20};
  mc.work_offsets[1] = {-10, 0, 0};
  auto m = moves(run("G54 G0 X1 Y2 Z3\nG55 G0 X1 Y2 Z3", mc));
  check_point(m[0].end, 1, 2, 3);
  check_point(m[0].end + m[0].machine_offset, 101, 52, -17);
  check_point(m[1].end + m[1].machine_offset, -9, 2, 3);
}

TEST_CASE("the program starts at machine zero") {
  MachineConfig mc;
  mc.work_offsets[0] = {0, 0, -100};
  auto m = moves(run("G0 X0", mc));
  check_point(m[0].start, 0, 0, 100);
}

TEST_CASE("G92 renames the current position without moving") {
  auto p = run("G0 X10 Y10\nG92 X0 Y0\nG0 X5");
  auto m = moves(p);
  REQUIRE(m.size() == 2);
  check_point(m[1].start, 0, 0, 0);
  check_point(m[1].end, 5, 0, 0);
  check_point(m[1].end + m[1].machine_offset, 15, 10, 0);
}

TEST_CASE("G53 moves in machine coordinates for one line") {
  MachineConfig mc;
  mc.work_offsets[0] = {100, 0, 0};
  auto m = moves(run("G0 X0\nG53 G0 X0\nG0 X1", mc));
  check_point(m[1].end, -100, 0, 0);
  check_point(m[2].end, 1, 0, 0);
}

TEST_CASE("G28 goes to machine zero through the intermediate point") {
  MachineConfig mc;
  mc.work_offsets[0] = {0, 0, -50};
  auto m = moves(run("G0 X10 Y10 Z5\nG91 G28 Z0", mc));
  REQUIRE(m.size() == 3);
  check_point(m[1].end, 10, 10, 5);   // intermediate = +0 incremental
  check_point(m[2].end, 10, 10, 50);  // machine Z0 = work Z50
}

TEST_CASE("G28 without axes homes all three") {
  auto m = moves(run("G0 X10 Y10 Z5\nG28"));
  check_point(m.back().end, 0, 0, 0);
}

TEST_CASE("G43 adds the tool length to machine Z, G49 removes it") {
  MachineConfig mc;
  mc.tools[1] = {6.0, 40.0};
  auto m = moves(run("T1 M6\nG43 H1\nG0 Z10\nG49\nG0 Z10", mc));
  const Segment& with = m[m.size() - 2];
  CHECK((with.end + with.machine_offset).z == Approx(50));
  CHECK((m.back().end + m.back().machine_offset).z == Approx(10));
}

TEST_CASE("G43 with an unknown tool warns with GC022") {
  auto p = run("G43 H7");
  CHECK(has_code(p.diagnostics, "GC022"));
}

TEST_CASE("M6 loads the selected tool and retracts Z first") {
  MachineConfig mc;
  mc.work_offsets[0] = {0, 0, -100};
  auto p = run("G0 X0 Y0 Z5\nT3\nM6\nG1 X1 F10", mc);
  REQUIRE(p.segments.size() == 4);
  CHECK(p.segments[1].type == MoveType::Rapid);
  check_point(p.segments[1].end, 0, 0, 100);
  CHECK(p.segments[2].type == MoveType::ToolChange);
  CHECK(p.segments[2].tool == 3);
  CHECK(p.segments[3].tool == 3);
}

TEST_CASE("M6 without T is a GC021 warning") {
  auto p = run("M6");
  CHECK(has_code(p.diagnostics, "GC021"));
  CHECK(p.segments.empty());
}

TEST_CASE("G4 dwell makes a dwell segment; without P it is GC017") {
  auto p = run("G4 P1.5");
  REQUIRE(p.segments.size() == 1);
  CHECK(p.segments[0].type == MoveType::Dwell);
  CHECK(p.segments[0].dwell_s == 1.5);
  CHECK(has_code(run("G4").diagnostics, "GC017"));
}

TEST_CASE("M0 and M1 make pauses") {
  auto p = run("M0\nM1");
  REQUIRE(p.segments.size() == 2);
  CHECK(p.segments[0].type == MoveType::Pause);
}

TEST_CASE("M30 ends the program; later lines are ignored with GC024") {
  auto p = run("G0 X1\nM30\nG0 X2\nG0 X3");
  CHECK(p.ended);
  CHECK(moves(p).size() == 1);
  CHECK(count_code(p.diagnostics, "GC024") == 1);
}

TEST_CASE("axis words after G80 are GC013") {
  auto p = run("G80\nX10");
  CHECK(has_code(p.diagnostics, "GC013"));
}

TEST_CASE("unused letters warn with GC023") {
  auto p = run("G1 X1 E5 F100");
  CHECK(has_code(p.diagnostics, "GC023"));
  CHECK(moves(p).size() == 1);
}

TEST_CASE("an error on one line doesn't stop the rest") {
  auto p = run("G1 X1 F100\nG1 X#2\nG1 X3");
  CHECK(has_code(p.diagnostics, "GC004"));
  auto m = moves(p);
  REQUIRE(m.size() == 2);  // line 2 keeps only its G1, which has nothing to move to
  CHECK(m.back().end.x == 3.0);
}

TEST_CASE("line numbers on segments point at the source line") {
  auto m = moves(run("(header)\n\nG0 X1\nG0 X2"));
  CHECK(m[0].line == 3);
  CHECK(m[1].line == 4);
}

TEST_CASE("line_count counts the last line without a newline") {
  CHECK(run("G0 X1\nG0 X2").line_count == 2);
  CHECK(run("G0 X1\nG0 X2\n").line_count == 2);
  CHECK(run("").line_count == 0);
}

TEST_CASE("machine config: parse a full file") {
  auto m = parse_machine_config(R"({
        "name": "mill",
        "max_rate_mm_min": {"x": 1000, "z": 500},
        "accel_mm_s2": {"y": 50},
        "travel_mm": {"x": [-10, 10]},
        "work_offsets": {"G55": {"x": 5}},
        "tools": {"2": {"diameter_mm": 3, "length_mm": 20}}
    })");
  CHECK(m.name == "mill");
  CHECK(m.max_rate_mm_min.x == 1000);
  CHECK(m.max_rate_mm_min.y == 5000);  // default kept
  CHECK(m.accel_mm_s2.y == 50);
  CHECK(m.has_travel);
  CHECK(m.travel_min[0] == -10);
  CHECK(m.work_offsets[1].x == 5);
  CHECK(m.tools.at(2).diameter_mm == 3);
}

TEST_CASE("machine config: bad input throws ConfigError with a reason") {
  CHECK_THROWS_AS(parse_machine_config("not json"), ConfigError);
  CHECK_THROWS_AS(parse_machine_config("[1]"), ConfigError);
  CHECK_THROWS_AS(parse_machine_config(R"({"max_rate_mm_min": {"x": -1}})"), ConfigError);
  CHECK_THROWS_AS(parse_machine_config(R"({"max_rate_mm_min": {"q": 1}})"), ConfigError);
  CHECK_THROWS_AS(parse_machine_config(R"({"travel_mm": {"x": [5, 1]}})"), ConfigError);
  CHECK_THROWS_AS(parse_machine_config(R"({"work_offsets": {"G60": {}}})"), ConfigError);
  CHECK_THROWS_AS(parse_machine_config(R"({"tools": {"abc": {}}})"), ConfigError);
  CHECK_THROWS_AS(parse_machine_config(R"({"colour": "red"})"), ConfigError);
  CHECK_THROWS_AS(load_machine_config("/no/such/file.json"), ConfigError);
}
