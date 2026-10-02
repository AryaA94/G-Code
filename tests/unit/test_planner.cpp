#include <numbers>

#include "gcodesim/planner.hpp"
#include "helpers.hpp"

using namespace test;

namespace {

// A machine that is effectively infinitely stiff, so time = distance / feed.
MachineConfig stiff() {
  MachineConfig m;
  m.max_rate_mm_min = {1e6, 1e6, 1e6};
  m.accel_mm_s2 = {1e9, 1e9, 1e9};
  return m;
}

double time_of(const std::string& text, const MachineConfig& m, bool lookahead = true) {
  PlannerOptions o;
  o.lookahead = lookahead;
  auto p = interpret(text, m);
  return plan(p.segments, m, o).total_time_s;
}

}  // namespace

TEST_CASE("with huge acceleration, time is distance over feed") {
  // plunge 10 mm at 600 mm/min = 1 s
  CHECK(time_of("G1 Z-10 F600", stiff()) == Approx(1.0).epsilon(1e-4));
}

TEST_CASE("a full circle takes 2*pi*r / feed") {
  double t = time_of("G0 X10 Y0\nG3 X10 Y0 I-10 J0 F600", stiff());
  CHECK(t == Approx(2 * std::numbers::pi * 10 / 10.0).epsilon(1e-3));
}

TEST_CASE("trapezoid: accelerate, cruise, decelerate") {
  // 100 mm at 60 mm/s with 100 mm/s^2: ramps take 0.6 s and 18 mm each
  PlannedBlock b;
  b.length = 100;
  b.accel = 100;
  trapezoid(b, 60);
  CHECK(b.peak == Approx(60));
  CHECK(b.t_accel == Approx(0.6));
  CHECK(b.t_decel == Approx(0.6));
  CHECK(b.t_cruise == Approx((100 - 36) / 60.0));
}

TEST_CASE("trapezoid: short move never reaches the feed (triangle)") {
  // 1 mm, a = 100: peak = sqrt(a * L) = 10 mm/s, time = 2 * 10 / 100
  PlannedBlock b;
  b.length = 1;
  b.accel = 100;
  trapezoid(b, 60);
  CHECK(b.t_cruise == 0.0);
  CHECK(b.peak == Approx(10));
  CHECK(b.duration() == Approx(0.2));
}

TEST_CASE("trapezoid with entry and exit speeds") {
  PlannedBlock b;
  b.length = 50;
  b.accel = 100;
  b.entry = 20;
  b.exit = 40;
  trapezoid(b, 60);
  // accelerate 20->60: 16 mm, decelerate 60->40: 10 mm, cruise 24 mm
  CHECK(b.t_accel == Approx(0.4));
  CHECK(b.t_decel == Approx(0.2));
  CHECK(b.t_cruise == Approx(24.0 / 60.0));
}

TEST_CASE("single move time matches the analytic trapezoid") {
  MachineConfig m;
  m.accel_mm_s2 = {100, 100, 100};
  m.max_rate_mm_min = {10000, 10000, 10000};
  // 100 mm at 3600 mm/min = 60 mm/s, from rest to rest
  CHECK(time_of("G1 X100 F3600", m) == Approx(0.6 + 0.6 + 64.0 / 60.0));
}

TEST_CASE("diagonal moves are limited by the slowest axis") {
  MachineConfig m = stiff();
  m.max_rate_mm_min = {6000, 6000, 600};  // Z is 10x slower
  // rapid straight down 10 mm: only Z moves, 600 mm/min = 10 mm/s
  CHECK(time_of("G0 Z-10", m) == Approx(1.0).epsilon(1e-4));
  // 45 degree move in XZ: Z covers 1/sqrt2 of the length, so speed = 10*sqrt2
  CHECK(time_of("G0 X10 Z-10", m) == Approx(1.0).epsilon(1e-4));
}

TEST_CASE("straight line split in two costs no extra time with look-ahead") {
  MachineConfig m;
  m.accel_mm_s2 = {100, 100, 100};
  double one = time_of("G1 X100 F3600", m);
  double two = time_of("G1 X50 F3600\nG1 X100", m);
  CHECK(two == Approx(one).epsilon(1e-9));
  CHECK(time_of("G1 X50 F3600\nG1 X100", m, false) > one + 0.5);
}

TEST_CASE("reversal forces a full stop even with look-ahead") {
  MachineConfig m;
  m.accel_mm_s2 = {100, 100, 100};
  double there = time_of("G1 X50 F3600", m);
  CHECK(time_of("G1 X50 F3600\nG1 X0", m) == Approx(2 * there));
}

TEST_CASE("junction speed grows with junction deviation") {
  MachineConfig m;
  m.accel_mm_s2 = {200, 200, 200};
  std::string zigzag = "G1 X10 Y5 F6000\nX20 Y0\nX30 Y5\nX40 Y0\nX50 Y5";
  m.junction_deviation_mm = 0.001;
  double tight = time_of(zigzag, m);
  m.junction_deviation_mm = 0.1;
  double loose = time_of(zigzag, m);
  CHECK(loose < tight);
  CHECK(time_of(zigzag, m, false) > tight);
}

TEST_CASE("every block respects its speed limits") {
  MachineConfig m;
  auto p = interpret("G0 X50 Y20\nG1 Z-1 F300\nG1 X60 F1200\nG2 X70 Y20 I5 J0\nG1 Y40\nG0 Z5", m);
  auto r = plan(p.segments, m);
  REQUIRE_FALSE(r.blocks.empty());
  CHECK(r.blocks.front().entry == 0.0);
  CHECK(r.blocks.back().exit == 0.0);
  for (std::size_t i = 0; i < r.blocks.size(); ++i) {
    const auto& b = r.blocks[i];
    CHECK(b.peak >= b.entry - 1e-9);
    CHECK(b.peak >= b.exit - 1e-9);
    // can go from entry to exit within the block's length
    CHECK(std::abs(b.exit * b.exit - b.entry * b.entry) <= 2 * b.accel * b.length + 1e-6);
    if (i > 0) CHECK(r.blocks[i - 1].exit == Approx(b.entry));
  }
}

TEST_CASE("dwells and tool changes add fixed time") {
  MachineConfig m = stiff();
  m.tool_change_time_s = 7;
  CHECK(time_of("G4 P2.5", m) == Approx(2.5));
  CHECK(time_of("T1 M6", m) == Approx(7));
}

TEST_CASE("moves with no feed add no time and don't break planning") {
  MachineConfig m = stiff();
  CHECK(time_of("G1 X10\nG1 X20 F600", m) == Approx(1.0).epsilon(1e-4));
}

TEST_CASE("segment times add up to the total and start times are increasing") {
  MachineConfig m;
  auto p = interpret("G0 X10\nG1 Y10 F500\nG4 P1\nG2 X20 Y0 R10\nG0 Z5", m);
  auto r = plan(p.segments, m);
  double sum = 0;
  for (std::size_t i = 0; i < r.segment_time_s.size(); ++i) {
    CHECK(r.segment_start_s[i] == Approx(sum).margin(1e-12));
    sum += r.segment_time_s[i];
  }
  CHECK(sum == Approx(r.total_time_s));
}

TEST_CASE("empty program takes no time") {
  MachineConfig m;
  auto r = plan({}, m);
  CHECK(r.total_time_s == 0.0);
  CHECK(r.blocks.empty());
}
