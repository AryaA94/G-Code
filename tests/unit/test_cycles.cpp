#include "helpers.hpp"

using namespace test;

namespace {
std::vector<double> z_stops(const std::vector<Segment>& m) {
  std::vector<double> out;
  for (const auto& s : m) out.push_back(s.end.z);
  return out;
}
}  // namespace

TEST_CASE("G81 drill: rapid over, rapid to R, feed to Z, rapid back") {
  auto m = moves(run("G0 X0 Y0 Z10\nG98 G81 X5 Y5 Z-3 R2 F100"));
  // [0] setup, then over the hole, down to R, feed, retract
  REQUIRE(m.size() == 5);
  check_point(m[1].end, 5, 5, 10);
  CHECK(m[2].type == MoveType::Rapid);
  CHECK(m[2].end.z == 2.0);
  CHECK(m[3].type == MoveType::Linear);
  CHECK(m[3].end.z == -3.0);
  CHECK(m[4].end.z == 10.0);  // G98: back to the starting height
  for (std::size_t i = 1; i < m.size(); ++i) CHECK(m[i].from_cycle);
}

TEST_CASE("G99 retracts to R instead of the initial Z") {
  auto m = moves(run("G0 Z10\nG99 G81 X5 Z-3 R2 F100"));
  CHECK(m.back().end.z == 2.0);
}

TEST_CASE("cycle repeats at each new position while it stays active") {
  auto p = run("G0 Z10\nG81 X0 Z-1 R1 F100\nX10\nX20 Y5\nG80\nG0 X0");
  int feeds = 0;
  for (const auto& s : moves(p))
    if (s.type == MoveType::Linear) ++feeds;
  CHECK(feeds == 3);
  CHECK(moves(p).back().from_cycle == false);
}

TEST_CASE("G82 adds a dwell at the bottom") {
  auto p = run("G0 Z5\nG82 X1 Z-2 R1 P0.5 F100");
  bool dwell = false;
  for (const auto& s : p.segments)
    if (s.type == MoveType::Dwell) dwell = s.dwell_s == 0.5;
  CHECK(dwell);
}

TEST_CASE("G83 pecks down in Q steps and clears chips at R") {
  auto m = moves(run("G0 X0 Y0 Z5\nG99 G83 X0 Y0 Z-10 R1 Q4 F100"));
  std::vector<double> feeds_to;
  for (const auto& s : m)
    if (s.type == MoveType::Linear) feeds_to.push_back(s.end.z);
  REQUIRE(feeds_to.size() == 3);
  CHECK(feeds_to[0] == Approx(-3));
  CHECK(feeds_to[1] == Approx(-7));
  CHECK(feeds_to[2] == Approx(-10));
  // after each peck it goes back up to R, then rapids down near the last depth
  auto zs = z_stops(m);
  CHECK(std::count(zs.begin(), zs.end(), 1.0) >= 3);
  CHECK(m.back().end.z == 1.0);
}

TEST_CASE("G83 without Q is GC019, missing R or Z is GC018") {
  CHECK(has_code(run("G83 X0 Z-5 R1 F100").diagnostics, "GC019"));
  CHECK(has_code(run("G81 X0 Z-5 F100").diagnostics, "GC018"));
  CHECK(has_code(run("G81 X0 R2 F100").diagnostics, "GC018"));
}

TEST_CASE("canned cycles under G91 are GC020") {
  CHECK(has_code(run("G91 G81 X1 Z-5 R1 F100").diagnostics, "GC020"));
}

TEST_CASE("a cycle starting below R first rapids up to R") {
  auto m = moves(run("G0 Z0\nG81 X5 Z-2 R3 F100"));
  CHECK(m[1].end.z == 3.0);
  CHECK(m[1].end.x == 0.0);
}

TEST_CASE("G0 cancels a cycle, so a new G81 records a new initial Z") {
  auto m = moves(run("G0 Z10\nG98 G81 X1 Z-1 R1 F100\nG0 Z20\nG81 X2 Z-1 R1\n"));
  CHECK(m.back().end.z == 20.0);
}
