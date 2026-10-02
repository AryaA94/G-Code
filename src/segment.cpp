#include "gcodesim/segment.hpp"

#include <algorithm>
#include <numbers>

namespace gcodesim {

namespace {
constexpr double kEps = 1e-9;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
}  // namespace

PlaneAxes axes_of(Plane p) {
  switch (p) {
    case Plane::XY:
      return {0, 1, 2};
    case Plane::ZX:
      return {2, 0, 1};
    case Plane::YZ:
      return {1, 2, 0};
  }
  return {0, 1, 2};
}

double arc_radius(const Segment& s) {
  auto ax = axes_of(s.plane);
  return std::hypot(s.start[ax.first] - s.center[ax.first], s.start[ax.second] - s.center[ax.second]);
}

double length(const Segment& s) {
  if (s.type == MoveType::Arc) {
    auto ax = axes_of(s.plane);
    double along = arc_radius(s) * std::abs(s.sweep);
    double rise = s.end[ax.normal] - s.start[ax.normal];
    return std::hypot(along, rise);
  }
  if (!s.is_motion()) return 0.0;
  return norm(s.end - s.start);
}

double arc_sweep(Vec3 start, Vec3 end, Vec3 center, Plane plane, bool clockwise) {
  auto ax = axes_of(plane);
  double a0 = std::atan2(start[ax.second] - center[ax.second], start[ax.first] - center[ax.first]);
  double a1 = std::atan2(end[ax.second] - center[ax.second], end[ax.first] - center[ax.first]);
  double sweep = a1 - a0;
  if (clockwise) {
    while (sweep >= -kEps) sweep -= kTwoPi;  // start == end -> full circle
    while (sweep < -kTwoPi - kEps) sweep += kTwoPi;
  } else {
    while (sweep <= kEps) sweep += kTwoPi;
    while (sweep > kTwoPi + kEps) sweep -= kTwoPi;
  }
  return sweep;
}

Vec3 point_at(const Segment& s, double t) {
  if (s.type != MoveType::Arc) return s.start + (s.end - s.start) * t;

  auto ax = axes_of(s.plane);
  double r0 = arc_radius(s);
  double a0 = std::atan2(s.start[ax.second] - s.center[ax.second], s.start[ax.first] - s.center[ax.first]);
  // The end radius can differ slightly from the start radius in real files
  // (rounded I/J). Blend between them so the path still ends exactly at `end`.
  double r1 = std::hypot(s.end[ax.first] - s.center[ax.first], s.end[ax.second] - s.center[ax.second]);
  double r = r0 + (r1 - r0) * t;
  double a = a0 + s.sweep * t;

  Vec3 p;
  p[ax.first] = s.center[ax.first] + r * std::cos(a);
  p[ax.second] = s.center[ax.second] + r * std::sin(a);
  p[ax.normal] = s.start[ax.normal] + (s.end[ax.normal] - s.start[ax.normal]) * t;
  if (t >= 1.0) return s.end;
  return p;
}

namespace {

Vec3 unit(Vec3 v) {
  double n = norm(v);
  return n < kEps ? Vec3{} : v * (1.0 / n);
}

Vec3 arc_tangent(const Segment& s, double t) {
  auto ax = axes_of(s.plane);
  Vec3 p = point_at(s, t);
  double rx = p[ax.first] - s.center[ax.first];
  double ry = p[ax.second] - s.center[ax.second];
  double dir = s.sweep >= 0 ? 1.0 : -1.0;
  // derivative of the helix with respect to t
  Vec3 d;
  d[ax.first] = -ry * std::abs(s.sweep) * dir;
  d[ax.second] = rx * std::abs(s.sweep) * dir;
  d[ax.normal] = s.end[ax.normal] - s.start[ax.normal];
  return unit(d);
}

}  // namespace

Vec3 start_direction(const Segment& s) {
  if (s.type == MoveType::Arc) return arc_tangent(s, 0.0);
  return unit(s.end - s.start);
}

Vec3 end_direction(const Segment& s) {
  if (s.type == MoveType::Arc) return arc_tangent(s, 1.0);
  return unit(s.end - s.start);
}

std::vector<Vec3> tessellate(const Segment& s, double tolerance) {
  if (s.type != MoveType::Arc) return {s.start, s.end};

  double r = arc_radius(s);
  int pieces = 1;
  if (r > tolerance) {
    // chord error of a piece spanning angle d is r * (1 - cos(d/2))
    double max_step = 2.0 * std::acos(std::max(-1.0, 1.0 - tolerance / r));
    pieces = static_cast<int>(std::ceil(std::abs(s.sweep) / std::max(max_step, 1e-6)));
  }
  pieces = std::clamp(pieces, 1, 10000);

  std::vector<Vec3> pts;
  pts.reserve(static_cast<std::size_t>(pieces) + 1);
  for (int i = 0; i <= pieces; ++i) pts.push_back(point_at(s, static_cast<double>(i) / pieces));
  return pts;
}

}  // namespace gcodesim
