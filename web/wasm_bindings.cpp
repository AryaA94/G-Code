// The functions the web page calls. Each takes plain strings and returns a
// JSON string, so the JavaScript side never touches C++ types.
#include <emscripten/emscripten.h>

#include <string>

#include "gcodesim/analysis.hpp"
#include "gcodesim/linter.hpp"
#include "gcodesim/report.hpp"
#include "gcodesim/svg.hpp"

using namespace gcodesim;
using nlohmann::json;

namespace {

std::string g_out;  // JS copies the result out right away, so one buffer is enough

const char* reply(const json& j) {
  g_out = j.dump();
  return g_out.c_str();
}

MachineConfig machine_from(const char* config_json) {
  std::string text = config_json ? config_json : "";
  return text.empty() ? MachineConfig{} : parse_machine_config(text);
}

}  // namespace

extern "C" {

// Full analysis: stats, diagnostics, every move as a polyline with timing,
// the speed profile, and the cycle time without look-ahead for comparison.
EMSCRIPTEN_KEEPALIVE
const char* gs_analyze(const char* gcode, const char* config_json) {
  try {
    MachineConfig machine = machine_from(config_json);
    Analysis a = analyze(gcode, machine);
    PlannerOptions stop_every_corner;
    stop_every_corner.lookahead = false;
    double no_lookahead = plan(a.program.segments, machine, stop_every_corner).total_time_s;

    json j = analysis_json(a);
    j["ok"] = true;
    j["machine"] = machine.name;
    j["toolpath"] = toolpath_json(a);
    j["time_no_lookahead_s"] = round_sig(no_lookahead);
    return reply(j);
  } catch (const std::exception& e) {
    return reply({{"ok", false}, {"error", e.what()}});
  }
}

// The same SVG `gcode-sim render` writes. color_by: 0 = feed, 1 = depth.
EMSCRIPTEN_KEEPALIVE
const char* gs_render_svg(const char* gcode, const char* config_json, int color_by, int show_rapids) {
  try {
    MachineConfig machine = machine_from(config_json);
    Program p = interpret(gcode, machine);
    SvgOptions o;
    o.color_by = color_by == 1 ? ColorBy::Depth : ColorBy::Feed;
    o.show_rapids = show_rapids != 0;
    g_out = render_svg(p.segments, o);
  } catch (const std::exception& e) {
    g_out = std::string("ERROR: ") + e.what();
  }
  return g_out.c_str();
}

EMSCRIPTEN_KEEPALIVE
const char* gs_rules() {
  json list = json::array();
  for (const auto& r : make_default_rules()) list.push_back({{"code", r->code()}, {"summary", r->summary()}});
  return reply(list);
}

}  // extern "C"
