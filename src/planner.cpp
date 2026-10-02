#include "gcodesim/planner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gcodesim {

namespace {

constexpr double kEps = 1e-9;
constexpr double kInf = std::numeric_limits<double>::infinity();

struct Piece {
  int segment = 0;
  Vec3 dir;
  double length = 0.0;
  double v_nominal = 0.0;  // mm/s
  double accel = 0.0;      // mm/s^2
  bool stop_before = false;
};

// The fastest a straight move in direction `u` can go without any single axis
// passing its own limit: each axis only covers |u_i| of the distance.
double limit_along(Vec3 u, Vec3 per_axis) {
  double v = kInf;
  for (int a = 0; a < 3; ++a)
    if (std::abs(u[a]) > kEps) v = std::min(v, per_axis[a] / std::abs(u[a]));
  return v;
}

// Grbl's junction deviation: pretend the corner is a circular arc that stays
// within `deviation` of the sharp corner, and allow the speed at which the
// centripetal acceleration on that arc equals `accel`.
double junction_speed(Vec3 prev_dir, Vec3 next_dir, double accel, double deviation) {
  double cos_theta = -dot(prev_dir, next_dir);  // theta = angle between the two moves' lines
  if (cos_theta > 0.999999) return 0.0;         // full reversal
  if (cos_theta < -0.999999) return kInf;       // straight on
  double sin_half = std::sqrt(0.5 * (1.0 - cos_theta));
  return std::sqrt(accel * deviation * sin_half / (1.0 - sin_half));
}

}  // namespace

void trapezoid(PlannedBlock& b, double vmax) {
  const double a = b.accel, L = b.length, v0 = b.entry, v1 = b.exit;
  double d_acc = (vmax * vmax - v0 * v0) / (2.0 * a);
  double d_dec = (vmax * vmax - v1 * v1) / (2.0 * a);
  if (d_acc + d_dec <= L) {
    b.peak = vmax;
    b.t_accel = (vmax - v0) / a;
    b.t_decel = (vmax - v1) / a;
    b.t_cruise = (L - d_acc - d_dec) / vmax;
  } else {
    // never reaches vmax: accelerate to a peak and immediately slow down
    double peak = std::sqrt(std::max(0.0, a * L + 0.5 * (v0 * v0 + v1 * v1)));
    peak = std::max({peak, v0, v1});
    b.peak = peak;
    b.t_accel = (peak - v0) / a;
    b.t_decel = (peak - v1) / a;
    b.t_cruise = 0.0;
  }
}

PlanResult plan(const std::vector<Segment>& segments, const MachineConfig& machine,
                const PlannerOptions& options) {
  PlanResult result;
  result.segment_time_s.assign(segments.size(), 0.0);
  result.segment_start_s.assign(segments.size(), 0.0);

  // 1. Cut everything into straight pieces with their own limits.
  std::vector<Piece> pieces;
  std::vector<double> fixed_time(segments.size(), 0.0);  // dwells and tool changes
  bool stop_next = true;                                 // the machine starts at rest

  for (std::size_t i = 0; i < segments.size(); ++i) {
    const Segment& s = segments[i];
    if (s.type == MoveType::Dwell) fixed_time[i] = s.dwell_s;
    if (s.type == MoveType::ToolChange) fixed_time[i] = machine.tool_change_time_s;
    if (!s.is_motion()) {
      stop_next = true;
      continue;
    }
    if (s.type != MoveType::Rapid && s.feed <= 0.0) {
      // No feed: the machine would refuse to run this (LN003); it adds no time here.
      stop_next = true;
      continue;
    }

    std::vector<Vec3> pts = tessellate(s, options.arc_tolerance);
    for (std::size_t k = 1; k < pts.size(); ++k) {
      Vec3 d = pts[k] - pts[k - 1];
      double len = norm(d);
      if (len < kEps) continue;
      Piece p;
      p.segment = static_cast<int>(i);
      p.dir = d * (1.0 / len);
      p.length = len;
      double axis_limit = limit_along(p.dir, machine.max_rate_mm_min) / 60.0;
      p.v_nominal = s.type == MoveType::Rapid ? axis_limit : std::min(s.feed / 60.0, axis_limit);
      p.accel = limit_along(p.dir, machine.accel_mm_s2);
      p.stop_before = stop_next || !options.lookahead;
      stop_next = false;
      pieces.push_back(p);
    }
  }

  // 2. Highest allowed speed at the start of each piece.
  const std::size_t n = pieces.size();
  std::vector<double> entry_max(n + 1, 0.0);  // entry_max[n] = end of program, at rest
  for (std::size_t k = 0; k < n; ++k) {
    if (pieces[k].stop_before) continue;
    const Piece& prev = pieces[k - 1];
    const Piece& cur = pieces[k];
    double v =
        junction_speed(prev.dir, cur.dir, std::min(prev.accel, cur.accel), machine.junction_deviation_mm);
    entry_max[k] = std::min({v, prev.v_nominal, cur.v_nominal});
  }

  // 3. Backward pass: each piece must be able to slow down to what comes next.
  std::vector<double> entry(n + 1, 0.0);
  for (std::size_t k = n; k-- > 0;) {
    double reachable = std::sqrt(entry[k + 1] * entry[k + 1] + 2.0 * pieces[k].accel * pieces[k].length);
    entry[k] = std::min(entry_max[k], reachable);
  }
  // 4. Forward pass: and must be able to speed up to it from what came before.
  for (std::size_t k = 0; k < n; ++k) {
    double reachable = std::sqrt(entry[k] * entry[k] + 2.0 * pieces[k].accel * pieces[k].length);
    entry[k + 1] = std::min(entry[k + 1], reachable);
  }

  // 5. Profiles and times, in program order.
  double t = 0.0;
  std::size_t k = 0;
  for (std::size_t i = 0; i < segments.size(); ++i) {
    result.segment_start_s[i] = t;
    double seg_time = fixed_time[i];
    while (k < n && pieces[k].segment == static_cast<int>(i)) {
      PlannedBlock b;
      b.segment = pieces[k].segment;
      b.length = pieces[k].length;
      b.accel = pieces[k].accel;
      b.entry = entry[k];
      b.exit = entry[k + 1];
      trapezoid(b, pieces[k].v_nominal);
      b.start_time = t + seg_time;
      seg_time += b.duration();
      result.blocks.push_back(b);
      ++k;
    }
    result.segment_time_s[i] = seg_time;
    t += seg_time;
  }
  result.total_time_s = t;
  return result;
}

}  // namespace gcodesim
