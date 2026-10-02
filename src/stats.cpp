#include "gcodesim/stats.hpp"

#include <algorithm>

namespace gcodesim {

void Bounds::add(Vec3 p) {
  if (empty) {
    min = max = p;
    empty = false;
    return;
  }
  for (int a = 0; a < 3; ++a) {
    min[a] = std::min(min[a], p[a]);
    max[a] = std::max(max[a], p[a]);
  }
}

Stats compute_stats(const Program& program, const PlanResult& plan) {
  Stats st;
  st.lines = program.line_count;
  st.total_time_s = plan.total_time_s;

  const auto& segs = program.segments;
  for (std::size_t i = 0; i < segs.size(); ++i) {
    const Segment& s = segs[i];
    double t = i < plan.segment_time_s.size() ? plan.segment_time_s[i] : 0.0;
    switch (s.type) {
      case MoveType::Rapid:
        ++st.rapid_moves;
        break;
      case MoveType::Linear:
        ++st.linear_moves;
        break;
      case MoveType::Arc:
        ++st.arc_moves;
        break;
      case MoveType::ToolChange:
        ++st.tool_changes;
        break;
      case MoveType::Dwell:
        ++st.dwells;
        break;
      case MoveType::Pause:
        ++st.pauses;
        break;
    }
    if (s.type == MoveType::ToolChange &&
        std::find(st.tools.begin(), st.tools.end(), s.tool) == st.tools.end())
      st.tools.push_back(s.tool);

    if (!s.is_motion()) {
      st.other_time_s += t;
      continue;
    }
    for (Vec3 p : tessellate(s, 0.01)) {
      st.all_bounds.add(p);
      if (s.is_cutting()) st.cut_bounds.add(p);
    }
    if (s.type == MoveType::Rapid) {
      st.rapid_length_mm += length(s);
      st.rapid_time_s += t;
    } else {
      st.cut_length_mm += length(s);
      st.cut_time_s += t;
      if (s.feed > 0.0) {
        st.min_feed_mm_min = st.min_feed_mm_min == 0.0 ? s.feed : std::min(st.min_feed_mm_min, s.feed);
        st.max_feed_mm_min = std::max(st.max_feed_mm_min, s.feed);
      }
      if (s.spindle_on) st.max_spindle_rpm = std::max(st.max_spindle_rpm, s.spindle_rpm);
    }
  }
  return st;
}

}  // namespace gcodesim
