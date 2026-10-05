#include "gcodesim/report.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace gcodesim {

using nlohmann::json;

namespace {

std::string fixed(double v, int decimals) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
  return buf;
}

json xyz(Vec3 v) {
  return {round_sig(v.x), round_sig(v.y), round_sig(v.z)};
}

json bounds_json(const Bounds& b) {
  if (b.empty) return nullptr;
  return {{"min", xyz(b.min)}, {"max", xyz(b.max)}};
}

const char* type_name(MoveType t) {
  switch (t) {
    case MoveType::Rapid:
      return "rapid";
    case MoveType::Linear:
      return "linear";
    case MoveType::Arc:
      return "arc";
    case MoveType::Dwell:
      return "dwell";
    case MoveType::ToolChange:
      return "tool_change";
    case MoveType::Pause:
      return "pause";
  }
  return "?";
}

}  // namespace

double round_sig(double v, int digits) {
  if (v == 0.0 || !std::isfinite(v)) return v == 0.0 ? 0.0 : v;
  double mag = std::pow(10.0, digits - 1 - static_cast<int>(std::floor(std::log10(std::abs(v)))));
  double r = std::round(v * mag) / mag;
  return r == 0.0 ? 0.0 : r;  // no "-0"
}

std::string format_duration(double s) {
  long tenths = std::lround(s * 10.0);
  if (tenths < 600) return fixed(static_cast<double>(tenths) / 10.0, 1) + " s";
  long h = tenths / 36000, m = (tenths / 600) % 60;
  double sec = static_cast<double>(tenths % 600) / 10.0;
  char buf[64];
  if (h > 0)
    std::snprintf(buf, sizeof buf, "%ld h %02ld min %04.1f s", h, m, sec);
  else
    std::snprintf(buf, sizeof buf, "%ld min %04.1f s", m, sec);
  return buf;
}

std::string stats_text(const Analysis& a, const MachineConfig& machine) {
  const Stats& s = a.stats;
  std::ostringstream o;
  o << "Machine        " << machine.name << "\n";
  o << "Cycle time     " << format_duration(s.total_time_s) << "\n";
  o << "  cutting      " << format_duration(s.cut_time_s) << "\n";
  o << "  rapids       " << format_duration(s.rapid_time_s) << "\n";
  o << "  other        " << format_duration(s.other_time_s) << " (tool changes, dwells)\n";
  o << "Distance       " << fixed(s.cut_length_mm, 1) << " mm cutting, " << fixed(s.rapid_length_mm, 1)
    << " mm rapid\n";
  o << "Moves          " << s.linear_moves << " linear, " << s.arc_moves << " arcs, " << s.rapid_moves
    << " rapids\n";
  if (!s.cut_bounds.empty) {
    Vec3 sz = s.cut_bounds.size();
    o << "Cut extent     X " << fixed(s.cut_bounds.min.x, 3) << " .. " << fixed(s.cut_bounds.max.x, 3)
      << "  Y " << fixed(s.cut_bounds.min.y, 3) << " .. " << fixed(s.cut_bounds.max.y, 3) << "  Z "
      << fixed(s.cut_bounds.min.z, 3) << " .. " << fixed(s.cut_bounds.max.z, 3) << "\n";
    o << "               (" << fixed(sz.x, 1) << " x " << fixed(sz.y, 1) << " x " << fixed(sz.z, 1)
      << " mm)\n";
  }
  if (s.max_feed_mm_min > 0)
    o << "Feed           " << fixed(s.min_feed_mm_min, 0) << " .. " << fixed(s.max_feed_mm_min, 0)
      << " mm/min\n";
  o << "Tools          ";
  if (s.tools.empty()) o << "none";
  for (std::size_t i = 0; i < s.tools.size(); ++i) o << (i ? ", " : "") << 'T' << s.tools[i];
  o << " (" << s.tool_changes << " change" << (s.tool_changes == 1 ? "" : "s") << ")\n";
  auto diags = a.diagnostics();
  o << "Diagnostics    " << count(diags, Severity::Error) << " error(s), " << count(diags, Severity::Warning)
    << " warning(s)\n";
  return o.str();
}

json stats_json(const Analysis& a) {
  const Stats& s = a.stats;
  json tools = json::array();
  for (int t : s.tools) tools.push_back(t);
  return {
      {"lines", s.lines},
      {"time_s",
       {{"total", round_sig(s.total_time_s)},
        {"cutting", round_sig(s.cut_time_s)},
        {"rapid", round_sig(s.rapid_time_s)},
        {"other", round_sig(s.other_time_s)}}},
      {"length_mm", {{"cutting", round_sig(s.cut_length_mm)}, {"rapid", round_sig(s.rapid_length_mm)}}},
      {"moves",
       {{"rapid", s.rapid_moves},
        {"linear", s.linear_moves},
        {"arc", s.arc_moves},
        {"tool_changes", s.tool_changes},
        {"dwells", s.dwells},
        {"pauses", s.pauses}}},
      {"cut_bounds", bounds_json(s.cut_bounds)},
      {"all_bounds", bounds_json(s.all_bounds)},
      {"feed_mm_min", {{"min", round_sig(s.min_feed_mm_min)}, {"max", round_sig(s.max_feed_mm_min)}}},
      {"max_spindle_rpm", round_sig(s.max_spindle_rpm)},
      {"tools", tools},
  };
}

std::string diagnostics_text(const std::vector<Diagnostic>& diags, std::string_view filename) {
  std::string out;
  for (const auto& d : diags) out += format_diagnostic(d, filename) + "\n";
  out += std::to_string(count(diags, Severity::Error)) + " error(s), " +
         std::to_string(count(diags, Severity::Warning)) + " warning(s)\n";
  return out;
}

json diagnostics_json(const std::vector<Diagnostic>& diags) {
  json list = json::array();
  for (const auto& d : diags) {
    list.push_back({{"severity", std::string(to_string(d.severity))},
                    {"code", d.code},
                    {"line", d.line},
                    {"column", d.column},
                    {"message", d.message}});
  }
  return {{"errors", count(diags, Severity::Error)},
          {"warnings", count(diags, Severity::Warning)},
          {"diagnostics", list}};
}

json analysis_json(const Analysis& a) {
  return {{"stats", stats_json(a)}, {"lint", diagnostics_json(a.diagnostics())}};
}

json toolpath_json(const Analysis& a) {
  json moves = json::array();
  const auto& segs = a.program.segments;
  for (std::size_t i = 0; i < segs.size(); ++i) {
    const Segment& s = segs[i];
    json pts = json::array();
    if (s.is_motion()) {
      for (Vec3 p : tessellate(s, 0.02)) pts.push_back(xyz(p));
    } else {
      pts.push_back(xyz(s.start));
    }
    moves.push_back({{"type", type_name(s.type)},
                     {"line", s.line},
                     {"feed", round_sig(s.feed)},
                     {"tool", s.tool},
                     {"rpm", s.spindle_on ? round_sig(s.spindle_rpm) : 0.0},
                     {"t0", round_sig(a.plan.segment_start_s[i])},
                     {"dt", round_sig(a.plan.segment_time_s[i])},
                     {"points", pts}});
  }
  // speed profile, one row per planned block: start time, entry, peak, exit, phase times
  json profile = json::array();
  for (const auto& b : a.plan.blocks) {
    profile.push_back({round_sig(b.start_time), round_sig(b.entry), round_sig(b.peak), round_sig(b.exit),
                       round_sig(b.t_accel), round_sig(b.t_cruise), round_sig(b.t_decel)});
  }
  return {{"moves", moves}, {"profile", profile}};
}

}  // namespace gcodesim
