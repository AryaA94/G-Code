#include "gcodesim/interpreter.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace gcodesim {

namespace {

constexpr double kEps = 1e-9;
constexpr double kPeckClearanceMm = 0.254;  // how far above the last peck G83 rapids back to (LinuxCNC value)

// Modal state: everything a G-code line can change that stays in effect for
// the lines after it.
struct ModalState {
  int motion = 0;  // tenths: 0, 10, 20, 30, 800 (none), 810, 820, 830
  Plane plane = Plane::XY;
  bool inches = false;
  bool incremental = false;
  bool arc_center_absolute = false;  // G90.1
  int work_offset = 0;               // 0..5 for G54..G59
  Vec3 g92;
  double tool_length = 0.0;  // active G43 offset
  double feed = 0.0;         // mm/min
  double spindle_rpm = 0.0;
  int spindle_dir = 0;  // +1 M3, -1 M4, 0 off
  int tool_selected = 0;
  int tool_loaded = 0;
  bool retract_to_r = false;  // G99; false = G98

  // canned cycle values, sticky while the cycle stays active
  bool cycle_active = false;
  double cycle_initial_z = 0.0;
  double cycle_r = 0.0, cycle_z = 0.0, cycle_q = 0.0, cycle_p = 0.0;
  bool has_cycle_r = false, has_cycle_z = false;
};

class Interpreter {
 public:
  Interpreter(const MachineConfig& machine, Program& out) : machine_(machine), out_(out) {
    pos_ = Vec3{} - offset();  // the machine starts at its home position, machine zero
  }

  // Returns false once the program has ended and nothing more should run.
  bool step(const Block& b) {
    if (out_.ended) {
      note(Severity::Info, "GC024", b.line, 0, "lines after the program end (M2/M30) are ignored");
      return false;
    }
    execute(b);
    return true;
  }

 private:
  const MachineConfig& machine_;
  Program& out_;
  ModalState s_;
  Vec3 pos_;  // tool tip, work coordinates, mm

  double unit() const { return s_.inches ? kMmPerInch : 1.0; }

  Vec3 offset() const {
    Vec3 o = machine_.work_offsets[static_cast<std::size_t>(s_.work_offset)] + s_.g92;
    o.z += s_.tool_length;
    return o;
  }

  // Changing an offset doesn't move the machine, only what "here" is called.
  template <typename F>
  void change_offset(F&& change) {
    Vec3 machine_pos = pos_ + offset();
    change();
    pos_ = machine_pos - offset();
  }

  void note(Severity sev, std::string code, int line, int col, std::string msg) {
    out_.diagnostics.push_back({sev, std::move(code), line, col, std::move(msg)});
  }

  Segment base(MoveType type, int line) const {
    Segment seg;
    seg.type = type;
    seg.start = pos_;
    seg.end = pos_;
    seg.feed = s_.feed;
    seg.spindle_rpm = s_.spindle_rpm;
    seg.spindle_on = s_.spindle_dir != 0;
    seg.tool = s_.tool_loaded;
    seg.machine_offset = offset();
    seg.plane = s_.plane;
    seg.line = line;
    return seg;
  }

  void move_to(MoveType type, Vec3 target, int line) {
    Segment seg = base(type, line);
    seg.end = target;
    out_.segments.push_back(seg);
    pos_ = target;
  }

  static bool has_axis(const Block& b) { return b.has('X') || b.has('Y') || b.has('Z'); }

  // Where the axis words of this block point, in work coordinates.
  Vec3 target_of(const Block& b, bool machine_coords) const {
    Vec3 t = pos_;
    const char letters[3] = {'X', 'Y', 'Z'};
    for (int a = 0; a < 3; ++a) {
      const auto& v = b.get(letters[a]);
      if (!v) continue;
      double mm = *v * unit();
      if (machine_coords)
        t[a] = mm - offset()[a];
      else if (s_.incremental)
        t[a] = pos_[a] + mm;
      else
        t[a] = mm;
    }
    return t;
  }

  void execute(const Block& b) {
    // Unused letters (E from 3D printers, D, L...) would silently change
    // nothing, which is worth a warning.
    for (char c : {'D', 'E', 'L'}) {  // the parser already rejects A B C U V W, and drops N O
      if (b.has(c)) {
        note(Severity::Warning, "GC023", b.line, b.column(c),
             std::string("'") + c + "' is not used by this simulator and was ignored");
      }
    }

    // Order follows RS-274/NGC section 3.8, except units are applied
    // first so "G20 F10" means 10 inches/min.
    if (b.has_g(200)) s_.inches = true;
    if (b.has_g(210)) s_.inches = false;
    if (b.has('F')) s_.feed = *b.get('F') * unit();
    if (b.has('S')) s_.spindle_rpm = std::abs(*b.get('S'));
    if (b.has('T')) s_.tool_selected = static_cast<int>(std::lround(*b.get('T')));

    if (b.has_m(6)) {
      if (s_.tool_selected == 0) {
        note(Severity::Warning, "GC021", b.line, 0, "M6 tool change with no tool selected (T)");
      } else {
        // Machines retract Z to machine zero before they swap tools.
        Vec3 up = pos_;
        up.z = -offset().z;
        if (up.z > pos_.z + kEps) move_to(MoveType::Rapid, up, b.line);
        Segment seg = base(MoveType::ToolChange, b.line);
        s_.tool_loaded = s_.tool_selected;
        seg.tool = s_.tool_loaded;
        out_.segments.push_back(seg);
      }
    }
    if (b.has_m(3)) s_.spindle_dir = 1;
    if (b.has_m(4)) s_.spindle_dir = -1;
    if (b.has_m(5)) s_.spindle_dir = 0;

    if (b.has_g(40)) {
      if (!b.has('P')) {
        note(Severity::Error, "GC017", b.line, 0, "G4 dwell needs a time in seconds (P)");
      } else {
        Segment seg = base(MoveType::Dwell, b.line);
        seg.dwell_s = std::max(0.0, *b.get('P'));
        out_.segments.push_back(seg);
      }
    }

    if (b.has_g(170)) s_.plane = Plane::XY;
    if (b.has_g(180)) s_.plane = Plane::ZX;
    if (b.has_g(190)) s_.plane = Plane::YZ;

    if (b.has_g(430)) {
      int h = b.has('H') ? static_cast<int>(std::lround(*b.get('H'))) : s_.tool_loaded;
      auto it = machine_.tools.find(h);
      double len = 0.0;
      if (it == machine_.tools.end()) {
        note(Severity::Warning, "GC022", b.line, 0,
             "tool " + std::to_string(h) + " is not in the machine config; length offset of 0 used");
      } else {
        len = it->second.length_mm;
      }
      change_offset([&] { s_.tool_length = len; });
    }
    if (b.has_g(490)) change_offset([&] { s_.tool_length = 0.0; });

    for (int i = 0; i < 6; ++i)
      if (b.has_g(540 + 10 * i)) change_offset([&] { s_.work_offset = i; });

    if (b.has_g(900)) s_.incremental = false;
    if (b.has_g(910)) s_.incremental = true;
    if (b.has_g(901)) s_.arc_center_absolute = true;
    if (b.has_g(911)) s_.arc_center_absolute = false;
    if (b.has_g(980)) s_.retract_to_r = false;
    if (b.has_g(990)) s_.retract_to_r = true;

    bool axes_used = false;
    if (b.has_g(920)) {
      // G92: "call where the tool is now X..Y..Z.."
      axes_used = true;
      const char letters[3] = {'X', 'Y', 'Z'};
      change_offset([&] {
        for (int a = 0; a < 3; ++a)
          if (b.has(letters[a])) s_.g92[a] += pos_[a] - *b.get(letters[a]) * unit();
      });
    } else if (b.has_g(280)) {
      axes_used = true;
      go_home(b);
    }

    motion(b, axes_used);

    if (b.has_m(0) || b.has_m(1)) out_.segments.push_back(base(MoveType::Pause, b.line));
    if (b.has_m(2) || b.has_m(30)) out_.ended = true;
  }

  void go_home(const Block& b) {
    const char letters[3] = {'X', 'Y', 'Z'};
    bool any = has_axis(b);
    Vec3 via = target_of(b, false);
    if (any) move_to(MoveType::Rapid, via, b.line);
    Vec3 home = pos_;
    for (int a = 0; a < 3; ++a)
      if (!any || b.has(letters[a])) home[a] = -offset()[a];  // machine zero
    move_to(MoveType::Rapid, home, b.line);
  }

  void motion(const Block& b, bool axes_used) {
    int new_motion = -1;
    for (int g : {0, 10, 20, 30, 800, 810, 820, 830})
      if (b.has_g(g)) new_motion = g;
    if (new_motion >= 0) {
      if (new_motion < 810) s_.cycle_active = false;
      s_.motion = new_motion;
    }
    if (axes_used) return;

    bool is_arc = s_.motion == 20 || s_.motion == 30;
    bool has_arc_words = b.has('I') || b.has('J') || b.has('K') || b.has('R');
    if (!has_axis(b) && !(is_arc && has_arc_words)) return;

    if (s_.motion == 800) {
      note(Severity::Error, "GC013", b.line, 0, "axis words given but no motion mode is active (after G80)");
      return;
    }
    bool machine_coords = b.has_g(530);
    Vec3 target = target_of(b, machine_coords);

    switch (s_.motion) {
      case 0:
        move_to(MoveType::Rapid, target, b.line);
        break;
      case 10:
        move_to(MoveType::Linear, target, b.line);
        break;
      case 20:
      case 30:
        arc(b, target, s_.motion == 20);
        break;
      default:
        canned_cycle(b);
        break;
    }
  }

  void arc(const Block& b, Vec3 target, bool clockwise) {
    PlaneAxes ax = axes_of(s_.plane);
    const char offset_letter[3] = {'I', 'J', 'K'};
    Vec3 center = pos_;

    if (b.has('R')) {
      double r = *b.get('R') * unit();
      double dx = target[ax.first] - pos_[ax.first];
      double dy = target[ax.second] - pos_[ax.second];
      double d = std::hypot(dx, dy);
      if (d < 1e-6) {
        note(Severity::Error, "GC016", b.line, b.column('R'),
             "an R-form arc cannot be a full circle; use I/J/K");
        return;
      }
      double half = d / 2.0;
      if (std::abs(r) < half - 1e-4) {
        note(Severity::Error, "GC015", b.line, b.column('R'),
             "arc radius " + std::to_string(std::abs(r)) + " mm is too small to reach the end point");
        return;
      }
      double h = std::sqrt(std::max(0.0, r * r - half * half));
      // Center sits left of the chord for a short CCW arc; CW or R<0 flips it.
      double side = (clockwise ? -1.0 : 1.0) * (r > 0 ? 1.0 : -1.0);
      center[ax.first] = pos_[ax.first] + dx / 2.0 - dy / d * h * side;
      center[ax.second] = pos_[ax.second] + dy / 2.0 + dx / d * h * side;
    } else {
      char l1 = offset_letter[ax.first];
      char l2 = offset_letter[ax.second];
      if (!b.has(l1) && !b.has(l2)) {
        note(Severity::Error, "GC014", b.line, 0,
             std::string("arc needs a center (") + l1 + "/" + l2 + ") or a radius (R)");
        return;
      }
      for (int a : {ax.first, ax.second}) {
        double v = b.get(offset_letter[a]).value_or(0.0) * unit();
        if (s_.arc_center_absolute)
          center[a] = b.has(offset_letter[a]) ? v : pos_[a];
        else
          center[a] = pos_[a] + v;
      }
    }
    center[ax.normal] = pos_[ax.normal];

    double r0 = std::hypot(pos_[ax.first] - center[ax.first], pos_[ax.second] - center[ax.second]);
    if (r0 < 1e-6) {
      note(Severity::Error, "GC025", b.line, 0, "arc radius is zero (center is at the start point)");
      return;
    }

    Segment seg = base(MoveType::Arc, b.line);
    seg.end = target;
    seg.center = center;
    seg.sweep = arc_sweep(pos_, target, center, s_.plane, clockwise);
    out_.segments.push_back(seg);
    pos_ = target;
  }

  void canned_cycle(const Block& b) {
    if (s_.incremental) {
      note(Severity::Error, "GC020", b.line, 0, "canned cycles in incremental mode (G91) are not supported");
      return;
    }
    if (!s_.cycle_active) {
      s_.cycle_active = true;
      s_.cycle_initial_z = pos_.z;
      s_.has_cycle_r = s_.has_cycle_z = false;
      s_.cycle_q = s_.cycle_p = 0.0;
    }
    if (b.has('R')) s_.cycle_r = *b.get('R') * unit(), s_.has_cycle_r = true;
    if (b.has('Z')) s_.cycle_z = *b.get('Z') * unit(), s_.has_cycle_z = true;
    if (b.has('Q')) s_.cycle_q = std::abs(*b.get('Q')) * unit();
    if (b.has('P')) s_.cycle_p = std::max(0.0, *b.get('P'));

    if (!s_.has_cycle_r || !s_.has_cycle_z) {
      note(Severity::Error, "GC018", b.line, 0, "drilling cycle needs both R (retract plane) and Z (depth)");
      return;
    }
    if (s_.motion == 830 && (s_.cycle_q <= 0.0 || (s_.cycle_r - s_.cycle_z) / s_.cycle_q > 10000.0)) {
      note(Severity::Error, "GC019", b.line, 0,
           "G83 peck drilling needs a peck depth Q greater than 0 (and at most 10000 pecks)");
      return;
    }

    const double r = s_.cycle_r, z = s_.cycle_z;
    const std::size_t first = out_.segments.size();
    Vec3 hole = pos_;
    if (b.has('X')) hole.x = *b.get('X') * unit();
    if (b.has('Y')) hole.y = *b.get('Y') * unit();

    auto rapid_z = [&](double zz) {
      Vec3 t = pos_;
      t.z = zz;
      if (std::abs(zz - pos_.z) > kEps) move_to(MoveType::Rapid, t, b.line);
    };
    auto feed_z = [&](double zz) {
      Vec3 t = pos_;
      t.z = zz;
      move_to(MoveType::Linear, t, b.line);
    };

    if (pos_.z < r) rapid_z(r);
    Vec3 over = hole;
    over.z = pos_.z;
    if (norm(over - pos_) > kEps) move_to(MoveType::Rapid, over, b.line);
    rapid_z(r);

    if (s_.motion == 830) {
      double depth = r;
      while (depth > z + kEps) {
        double next = std::max(depth - s_.cycle_q, z);
        if (depth < r - kEps) rapid_z(depth + kPeckClearanceMm);
        feed_z(next);
        depth = next;
        if (depth > z + kEps) rapid_z(r);
      }
    } else {
      feed_z(z);
      if (s_.motion == 820 && s_.cycle_p > 0.0) {
        Segment seg = base(MoveType::Dwell, b.line);
        seg.dwell_s = s_.cycle_p;
        out_.segments.push_back(seg);
      }
    }
    rapid_z(s_.retract_to_r ? r : std::max(r, s_.cycle_initial_z));
    for (std::size_t i = first; i < out_.segments.size(); ++i) out_.segments[i].from_cycle = true;
  }
};

}  // namespace

Program interpret(const std::vector<Block>& blocks, const MachineConfig& machine) {
  Program out;
  Interpreter interp(machine, out);
  for (const Block& b : blocks)
    if (!interp.step(b)) break;
  return out;
}

Program interpret(std::string_view text, const MachineConfig& machine) {
  // Line by line: parse one block, run it, forget it. Keeping every Block
  // around first costs ~500 bytes per line and made this 4x slower.
  Program out;
  // Roughly one move per line; growing the vector by doubling instead
  // copies every Segment ~20 times on a big file.
  out.segments.reserve(static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 16);
  Interpreter interp(machine, out);
  std::vector<Diagnostic> parse_diags;
  int line_no = 0;
  std::size_t pos = 0;
  bool running = true;
  while (pos < text.size()) {
    std::size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    ++line_no;
    if (running) {
      Block b = parse_block(lex_line(text.substr(pos, end - pos), line_no, parse_diags), parse_diags);
      if (!b.empty()) running = interp.step(b);
    }
    pos = end + 1;
  }
  out.line_count = line_no;
  if (!parse_diags.empty()) {
    out.diagnostics.insert(out.diagnostics.begin(), parse_diags.begin(),
                           parse_diags.end());  // parse errors first on a line
    std::stable_sort(out.diagnostics.begin(), out.diagnostics.end(),
                     [](const Diagnostic& a, const Diagnostic& b) { return a.line < b.line; });
  }
  return out;
}

}  // namespace gcodesim
