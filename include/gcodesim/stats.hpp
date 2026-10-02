#pragma once

#include <vector>

#include "gcodesim/interpreter.hpp"
#include "gcodesim/planner.hpp"

namespace gcodesim {

struct Bounds {
  Vec3 min, max;
  bool empty = true;

  void add(Vec3 p);
  Vec3 size() const { return empty ? Vec3{} : max - min; }
};

struct Stats {
  int lines = 0;
  int rapid_moves = 0, linear_moves = 0, arc_moves = 0;
  int tool_changes = 0, dwells = 0, pauses = 0;
  double rapid_length_mm = 0.0, cut_length_mm = 0.0;
  double total_time_s = 0.0, rapid_time_s = 0.0, cut_time_s = 0.0, other_time_s = 0.0;
  Bounds cut_bounds;       // work coordinates, cutting moves only
  Bounds all_bounds;       // including rapids
  std::vector<int> tools;  // in order of first use
  double min_feed_mm_min = 0.0, max_feed_mm_min = 0.0;
  double max_spindle_rpm = 0.0;
};

Stats compute_stats(const Program& program, const PlanResult& plan);

}  // namespace gcodesim
