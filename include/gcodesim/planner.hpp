#pragma once

#include <vector>

#include "gcodesim/machine.hpp"
#include "gcodesim/segment.hpp"

namespace gcodesim {

struct PlannerOptions {
  bool lookahead = true;         // false = come to a stop at every junction
  double arc_tolerance = 0.002;  // mm, arcs are split into chords this accurate
};

// One straight piece the machine actually drives, with its speed profile:
// speed up from `entry` to `peak`, hold, slow down to `exit`. Speeds in mm/s.
struct PlannedBlock {
  int segment = 0;  // index into the segment list it came from
  double length = 0.0;
  double entry = 0.0, peak = 0.0, exit = 0.0;
  double accel = 0.0;
  double t_accel = 0.0, t_cruise = 0.0, t_decel = 0.0;
  double start_time = 0.0;

  double duration() const { return t_accel + t_cruise + t_decel; }
};

struct PlanResult {
  double total_time_s = 0.0;
  std::vector<double> segment_time_s;   // one per input segment
  std::vector<double> segment_start_s;  // when each segment starts
  std::vector<PlannedBlock> blocks;
};

// Estimates how long the program takes on `machine`.
//
// Each move gets a trapezoidal speed profile limited by the per-axis rates
// and accelerations. With look-ahead the speed through each corner comes
// from Grbl's junction-deviation rule, then a backward and a forward pass
// make sure every block can actually reach its neighbours' speeds.
PlanResult plan(const std::vector<Segment>& segments, const MachineConfig& machine,
                const PlannerOptions& options = {});

// Time to cover `length` starting at v0, ending at v1, never above vmax,
// with acceleration `a`. Fills in the profile parts of `block`.
void trapezoid(PlannedBlock& block, double vmax);

}  // namespace gcodesim
