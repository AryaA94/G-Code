#include "gcodesim/linter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace gcodesim {

namespace {

constexpr double kEps = 1e-6;

std::string mm(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.3f", v);
  return buf;
}

Diagnostic make(Severity sev, const std::string& code, const Segment& s, std::string msg) {
  return {sev, code, s.line, 1, std::move(msg)};
}

// LN001: a rapid move below the top of the stock (Z0) crashes into the part.
class RapidIntoStock : public Rule {
 public:
  std::string code() const override { return "LN001"; }
  std::string summary() const override { return "rapid move (G0) goes below Z0, into the stock"; }
  void check(const std::vector<Segment>& segs, const MachineConfig&,
             std::vector<Diagnostic>& out) const override {
    for (const auto& s : segs) {
      if (s.type != MoveType::Rapid || s.from_cycle)
        continue;  // drilling cycles retract inside the hole on purpose
      bool moves_sideways = std::hypot(s.end.x - s.start.x, s.end.y - s.start.y) > kEps;
      if (s.end.z < -kEps && s.end.z < s.start.z - kEps) {
        out.push_back(make(Severity::Warning, code(), s,
                           "rapid move ends at Z" + mm(s.end.z) + " (below Z0); use G1 with a feed rate"));
      } else if (moves_sideways && std::min(s.start.z, s.end.z) < -kEps) {
        out.push_back(
            make(Severity::Warning, code(), s,
                 "rapid move travels sideways at Z" + mm(std::min(s.start.z, s.end.z)) + " (below Z0)"));
      }
    }
  }
};

// LN002: the machine would hit its limit switches. Reported once per axis
// and direction, otherwise one bad offset floods the output.
class OutOfTravel : public Rule {
 public:
  std::string code() const override { return "LN002"; }
  std::string summary() const override { return "move goes outside the machine's travel limits"; }
  void check(const std::vector<Segment>& segs, const MachineConfig& m,
             std::vector<Diagnostic>& out) const override {
    if (!m.has_travel) return;
    bool reported[3][2] = {};
    const char names[3] = {'X', 'Y', 'Z'};
    for (const auto& s : segs) {
      if (!s.is_motion()) continue;
      for (Vec3 p : tessellate(s, 0.05)) {
        Vec3 mp = p + s.machine_offset;
        for (int a = 0; a < 3; ++a) {
          auto ai = static_cast<std::size_t>(a);
          int side = mp[a] < m.travel_min[ai] - kEps ? 0 : mp[a] > m.travel_max[ai] + kEps ? 1 : -1;
          if (side < 0 || reported[a][side]) continue;
          reported[a][side] = true;
          out.push_back(make(Severity::Error, code(), s,
                             std::string(1, names[a]) + " reaches " + mm(mp[a]) +
                                 " mm (machine coords), outside travel [" + mm(m.travel_min[ai]) + ", " +
                                 mm(m.travel_max[ai]) + "]"));
        }
      }
    }
  }
};

// LN003: G1/G2/G3 before any F. Real controllers refuse to run this.
class MissingFeed : public Rule {
 public:
  std::string code() const override { return "LN003"; }
  std::string summary() const override { return "cutting move with no feed rate (F) set"; }
  void check(const std::vector<Segment>& segs, const MachineConfig&,
             std::vector<Diagnostic>& out) const override {
    for (const auto& s : segs) {
      if (s.is_cutting() && s.feed <= 0.0) {
        out.push_back(make(Severity::Error, code(), s, "cutting move with no feed rate (F) set"));
        return;  // the feed stays unset until the first F, so one message is enough
      }
    }
  }
};

// LN004: cutting with a stopped spindle breaks the tool. Reported once per
// stretch of such moves.
class SpindleOff : public Rule {
 public:
  std::string code() const override { return "LN004"; }
  std::string summary() const override { return "cutting move while the spindle is not running"; }
  void check(const std::vector<Segment>& segs, const MachineConfig&,
             std::vector<Diagnostic>& out) const override {
    bool in_bad_stretch = false;
    for (const auto& s : segs) {
      if (!s.is_cutting()) continue;
      bool bad = !s.spindle_on || s.spindle_rpm <= 0.0;
      if (bad && !in_bad_stretch) {
        out.push_back(make(Severity::Error, code(), s,
                           s.spindle_on ? "cutting move with spindle speed S0"
                                        : "cutting move while the spindle is not running (missing M3/M4?)"));
      }
      in_bad_stretch = bad;
    }
  }
};

// LN005: most machines stop the spindle for a tool change, but relying on
// that is a habit that eventually meets one that doesn't.
class ToolChangeSpindleOn : public Rule {
 public:
  std::string code() const override { return "LN005"; }
  std::string summary() const override { return "tool change (M6) while the spindle is on"; }
  void check(const std::vector<Segment>& segs, const MachineConfig&,
             std::vector<Diagnostic>& out) const override {
    for (const auto& s : segs) {
      if (s.type == MoveType::ToolChange && s.spindle_on) {
        out.push_back(
            make(Severity::Warning, code(), s, "tool change with the spindle still on (add M5 before M6)"));
      }
    }
  }
};

// LN006: I/J/K that don't put the start and end on the same circle. Uses the
// same tolerance as Grbl: 0.005 mm, or 0.1% of the radius if that is bigger.
class ArcRadiusMismatch : public Rule {
 public:
  std::string code() const override { return "LN006"; }
  std::string summary() const override {
    return "arc start and end are not the same distance from the center";
  }
  void check(const std::vector<Segment>& segs, const MachineConfig&,
             std::vector<Diagnostic>& out) const override {
    for (const auto& s : segs) {
      if (s.type != MoveType::Arc) continue;
      auto ax = axes_of(s.plane);
      double r0 = arc_radius(s);
      double r1 = std::hypot(s.end[ax.first] - s.center[ax.first], s.end[ax.second] - s.center[ax.second]);
      double diff = std::abs(r1 - r0);
      if (diff > 0.005 && diff > 0.001 * r0) {
        out.push_back(make(
            Severity::Error, code(), s,
            "arc radius is " + mm(r0) + " mm at the start but " + mm(r1) + " mm at the end (check I/J/K)"));
      }
    }
  }
};

// LN007: programmed feed faster than the machine allows. Reported once per
// distinct feed value.
class FeedAboveLimit : public Rule {
 public:
  std::string code() const override { return "LN007"; }
  std::string summary() const override { return "feed rate is above the machine's maximum"; }
  void check(const std::vector<Segment>& segs, const MachineConfig& m,
             std::vector<Diagnostic>& out) const override {
    std::set<double> seen;
    for (const auto& s : segs) {
      if (!s.is_cutting() || s.feed <= m.max_feed_mm_min + kEps) continue;
      if (!seen.insert(s.feed).second) continue;
      out.push_back(make(Severity::Warning, code(), s,
                         "feed " + mm(s.feed) + " mm/min is above the machine maximum of " +
                             mm(m.max_feed_mm_min) + " mm/min"));
    }
  }
};

// LN008: cutting before any M6. On most machines that means "whatever tool
// was left in the spindle", which is rarely the one the program expects.
class NoToolLoaded : public Rule {
 public:
  std::string code() const override { return "LN008"; }
  std::string summary() const override { return "cutting move before any tool was loaded (T.. M6)"; }
  void check(const std::vector<Segment>& segs, const MachineConfig&,
             std::vector<Diagnostic>& out) const override {
    for (const auto& s : segs) {
      if (s.is_cutting() && s.tool == 0) {
        out.push_back(make(Severity::Warning, code(), s, "cutting move before any tool was loaded (T.. M6)"));
        return;
      }
    }
  }
};

// LN009: plunging deeper than the tool is wide in a single move. A common
// milling rule of thumb: deeper than 1 x diameter per pass overloads the
// tool. Only the part below Z0 counts (the rest is air), drilling cycles
// are skipped (going deep is what a drill is for), and tools that aren't
// in the machine config are skipped because their diameter is unknown.
class DeepPlunge : public Rule {
 public:
  std::string code() const override { return "LN009"; }
  std::string summary() const override { return "cutting move plunges deeper than the tool's diameter"; }
  void check(const std::vector<Segment>& segs, const MachineConfig& m,
             std::vector<Diagnostic>& out) const override {
    for (const auto& s : segs) {
      if (!s.is_cutting() || s.from_cycle) continue;
      auto tool = m.tools.find(s.tool);
      if (tool == m.tools.end()) continue;
      double drop = std::min(s.start.z, 0.0) - s.end.z;
      double diameter = tool->second.diameter_mm;
      if (drop > diameter + kEps) {
        out.push_back(make(Severity::Warning, code(), s,
                           "cutting move plunges " + mm(drop) +
                               " mm into the stock, deeper than the loaded tool's diameter (" + mm(diameter) +
                               " mm)"));
      }
    }
  }
};

}  // namespace

std::vector<std::unique_ptr<Rule>> make_default_rules() {
  std::vector<std::unique_ptr<Rule>> rules;
  rules.push_back(std::make_unique<RapidIntoStock>());
  rules.push_back(std::make_unique<OutOfTravel>());
  rules.push_back(std::make_unique<MissingFeed>());
  rules.push_back(std::make_unique<SpindleOff>());
  rules.push_back(std::make_unique<ToolChangeSpindleOn>());
  rules.push_back(std::make_unique<ArcRadiusMismatch>());
  rules.push_back(std::make_unique<FeedAboveLimit>());
  rules.push_back(std::make_unique<NoToolLoaded>());
  rules.push_back(std::make_unique<DeepPlunge>());
  return rules;
}

std::vector<Diagnostic> lint(const std::vector<Segment>& segments, const MachineConfig& machine) {
  std::vector<Diagnostic> out;
  for (const auto& rule : make_default_rules()) rule->check(segments, machine, out);
  std::stable_sort(out.begin(), out.end(), [](const Diagnostic& a, const Diagnostic& b) {
    return a.line != b.line ? a.line < b.line : a.code < b.code;
  });
  return out;
}

}  // namespace gcodesim
