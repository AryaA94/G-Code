#include <numbers>

#include "helpers.hpp"

using namespace test;

constexpr double kPi = std::numbers::pi;

TEST_CASE("quarter arc CCW with I/J") {
  auto m = moves(run("G0 X10 Y0\nG3 X0 Y10 I-10 J0 F100"));
  REQUIRE(m.size() == 2);
  const Segment& a = m[1];
  CHECK(a.type == MoveType::Arc);
  check_point(a.center, 0, 0, 0);
  CHECK(a.sweep == Approx(kPi / 2));
  CHECK(length(a) == Approx(10 * kPi / 2));
}

TEST_CASE("same quarter going CW is three quarters") {
  auto m = moves(run("G0 X10 Y0\nG2 X0 Y10 I-10 J0 F100"));
  CHECK(m[1].sweep == Approx(-3 * kPi / 2));
  CHECK(length(m[1]) == Approx(10 * 3 * kPi / 2));
}

TEST_CASE("full circle when start equals end") {
  auto m = moves(run("G0 X5 Y0\nG2 X5 Y0 I-5 J0 F100"));
  CHECK(m[1].sweep == Approx(-2 * kPi));
  CHECK(length(m[1]) == Approx(2 * kPi * 5));
  auto m2 = moves(run("G0 X5 Y0\nG3 I-5 F100"));  // no end point given at all
  REQUIRE(m2.size() == 2);
  CHECK(m2[1].sweep == Approx(2 * kPi));
}

TEST_CASE("helix length combines the arc and the rise") {
  auto m = moves(run("G0 X5 Y0 Z0\nG3 X5 Y0 Z-3 I-5 J0 F100"));
  CHECK(length(m[1]) == Approx(std::hypot(2 * kPi * 5, 3.0)));
  check_point(point_at(m[1], 0.5), -5, 0, -1.5);
}

TEST_CASE("R form picks the short arc for positive R, long arc for negative") {
  auto shortarc = moves(run("G0 X0 Y0\nG2 X10 Y0 R10 F100"));
  CHECK(std::abs(shortarc[1].sweep) < kPi);
  auto longarc = moves(run("G0 X0 Y0\nG2 X10 Y0 R-10 F100"));
  CHECK(std::abs(longarc[1].sweep) > kPi);
  // both centers are 10 from start and end
  for (const auto* s : {&shortarc[1], &longarc[1]}) {
    CHECK(std::hypot(s->center.x, s->center.y) == Approx(10));
    CHECK(std::hypot(s->center.x - 10, s->center.y) == Approx(10));
  }
}

TEST_CASE("R form semicircle has its center at the midpoint") {
  auto m = moves(run("G0 X0 Y0\nG3 X10 Y0 R5 F100"));
  check_point(m[1].center, 5, 0, 0);
  CHECK(std::abs(m[1].sweep) == Approx(kPi));
}

TEST_CASE("R form CW short arc bulges the right way") {
  // Going from (0,0) to (10,0) clockwise, the short arc goes over the top
  // (y > 0) so the center is below the chord.
  auto m = moves(run("G0 X0 Y0\nG2 X10 Y0 R10 F100"));
  CHECK(m[1].center.y < 0);
  CHECK(point_at(m[1], 0.5).y > 0);
}

TEST_CASE("R too small is GC015, R full circle is GC016") {
  auto p = run("G0 X0 Y0\nG2 X10 Y0 R2 F100\nG2 X0 Y0 R5");
  CHECK(has_code(p.diagnostics, "GC015"));
  auto p2 = run("G0 X0 Y0\nG2 X0 Y0 R5 F100");
  CHECK(has_code(p2.diagnostics, "GC016"));
}

TEST_CASE("arc without center or radius is GC014") {
  auto p = run("G0 X0\nG2 X10 F100");
  CHECK(has_code(p.diagnostics, "GC014"));
  CHECK(moves(p).size() == 1);
}

TEST_CASE("arc with zero radius is GC025") {
  auto p = run("G0 X0\nG2 X0 Y0 I0 J0 F100");
  CHECK(has_code(p.diagnostics, "GC025"));
}

TEST_CASE("G18 arcs use K and I and go around Y") {
  // ZX plane: first axis Z, second X, so seen from +Y going from +X to +Z
  // is clockwise. That makes this G2 a quarter turn.
  auto m = moves(run("G18\nG0 X10 Y0 Z0\nG2 X0 Z10 I-10 K0 F100"));
  const Segment& a = m[1];
  check_point(a.center, 0, 0, 0);
  CHECK(std::abs(a.sweep) == Approx(kPi / 2).margin(1e-9));
  CHECK(length(a) == Approx(10 * kPi / 2));
  Vec3 mid = point_at(a, 0.5);
  CHECK(mid.y == Approx(0).margin(1e-12));
  CHECK(std::hypot(mid.x, mid.z) == Approx(10));
}

TEST_CASE("G19 arcs use J and K") {
  auto m = moves(run("G19\nG0 X0 Y10 Z0\nG2 Y0 Z10 J-10 K0 F100"));
  check_point(m[1].center, 0, 0, 0);
  Vec3 mid = point_at(m[1], 0.5);
  CHECK(mid.x == Approx(0).margin(1e-12));
  CHECK(std::hypot(mid.y, mid.z) == Approx(10));
}

TEST_CASE("G90.1 takes arc centers as absolute coordinates") {
  auto m = moves(run("G90.1\nG0 X20 Y10\nG3 X10 Y20 I10 J10 F100"));
  check_point(m[1].center, 10, 10, 0);
}

TEST_CASE("tessellation stays within tolerance and hits both ends") {
  auto m = moves(run("G0 X50 Y0\nG3 X-50 Y0 I-50 J0 F100"));
  auto pts = tessellate(m[1], 0.01);
  check_point(pts.front(), 50, 0, 0);
  check_point(pts.back(), -50, 0, 0);
  for (std::size_t i = 1; i < pts.size(); ++i) {
    Vec3 mid = (pts[i] + pts[i - 1]) * 0.5;
    double sag = 50 - std::hypot(mid.x, mid.y);
    CHECK(sag <= 0.0100001);
  }
}

TEST_CASE("start and end directions are tangent to the arc") {
  auto m = moves(run("G0 X10 Y0\nG3 X0 Y10 I-10 J0 F100"));
  Vec3 d0 = start_direction(m[1]);
  Vec3 d1 = end_direction(m[1]);
  check_point(d0, 0, 1, 0);
  check_point(d1, -1, 0, 0);
}

TEST_CASE("arc_sweep never returns zero for a closed arc") {
  Vec3 p{1, 0, 0};
  CHECK(arc_sweep(p, p, {}, Plane::XY, false) == Approx(2 * kPi));
  CHECK(arc_sweep(p, p, {}, Plane::XY, true) == Approx(-2 * kPi));
}
