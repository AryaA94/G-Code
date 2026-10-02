#include "gcodesim/machine.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace gcodesim {

namespace {

using nlohmann::json;

double positive(const json& j, const std::string& what) {
  if (!j.is_number()) throw ConfigError(what + " must be a number");
  double v = j.get<double>();
  if (!(v > 0.0)) throw ConfigError(what + " must be greater than 0");
  return v;
}

double non_negative(const json& j, const std::string& what) {
  if (!j.is_number()) throw ConfigError(what + " must be a number");
  double v = j.get<double>();
  if (v < 0.0) throw ConfigError(what + " must not be negative");
  return v;
}

double number(const json& j, const std::string& what) {
  if (!j.is_number()) throw ConfigError(what + " must be a number");
  return j.get<double>();
}

Vec3 read_xyz(const json& j, const std::string& what, Vec3 value, bool must_be_positive) {
  if (!j.is_object())
    throw ConfigError(what + " must be an object like {\"x\": ..., \"y\": ..., \"z\": ...}");
  for (auto& [key, v] : j.items()) {
    int axis = key == "x" ? 0 : key == "y" ? 1 : key == "z" ? 2 : -1;
    if (axis < 0) throw ConfigError(what + " has unknown axis \"" + key + "\"");
    value[axis] = must_be_positive ? positive(v, what + "." + key) : number(v, what + "." + key);
  }
  return value;
}

}  // namespace

MachineConfig parse_machine_config(std::string_view json_text) {
  json j;
  try {
    j = json::parse(json_text);
  } catch (const json::parse_error& e) {
    throw ConfigError(std::string("machine config is not valid JSON: ") + e.what());
  }
  if (!j.is_object()) throw ConfigError("machine config must be a JSON object");

  MachineConfig m;
  static const std::set<std::string> known = {"name",
                                              "max_rate_mm_min",
                                              "accel_mm_s2",
                                              "max_feed_mm_min",
                                              "junction_deviation_mm",
                                              "tool_change_time_s",
                                              "travel_mm",
                                              "work_offsets",
                                              "tools",
                                              "comment"};

  for (auto& [key, v] : j.items()) {
    if (!known.count(key)) throw ConfigError("unknown key \"" + key + "\" in machine config");
    if (key == "name") {
      if (!v.is_string()) throw ConfigError("name must be a string");
      m.name = v.get<std::string>();
    } else if (key == "max_rate_mm_min") {
      m.max_rate_mm_min = read_xyz(v, key, m.max_rate_mm_min, true);
    } else if (key == "accel_mm_s2") {
      m.accel_mm_s2 = read_xyz(v, key, m.accel_mm_s2, true);
    } else if (key == "max_feed_mm_min") {
      m.max_feed_mm_min = positive(v, key);
    } else if (key == "junction_deviation_mm") {
      m.junction_deviation_mm = non_negative(v, key);
    } else if (key == "tool_change_time_s") {
      m.tool_change_time_s = non_negative(v, key);
    } else if (key == "travel_mm") {
      if (!v.is_object()) throw ConfigError("travel_mm must be an object of [min, max] pairs");
      for (auto& [axis_name, range] : v.items()) {
        int axis = axis_name == "x" ? 0 : axis_name == "y" ? 1 : axis_name == "z" ? 2 : -1;
        if (axis < 0) throw ConfigError("travel_mm has unknown axis \"" + axis_name + "\"");
        if (!range.is_array() || range.size() != 2)
          throw ConfigError("travel_mm." + axis_name + " must be [min, max]");
        double lo = number(range[0], "travel_mm." + axis_name + "[0]");
        double hi = number(range[1], "travel_mm." + axis_name + "[1]");
        if (lo >= hi) throw ConfigError("travel_mm." + axis_name + " min must be less than max");
        m.travel_min[static_cast<std::size_t>(axis)] = lo;
        m.travel_max[static_cast<std::size_t>(axis)] = hi;
      }
      m.has_travel = true;
    } else if (key == "work_offsets") {
      if (!v.is_object()) throw ConfigError("work_offsets must be an object like {\"G54\": {\"x\": 0, ...}}");
      for (auto& [name, off] : v.items()) {
        static const char* names[] = {"G54", "G55", "G56", "G57", "G58", "G59"};
        int idx = -1;
        for (int i = 0; i < 6; ++i)
          if (name == names[i]) idx = i;
        if (idx < 0) throw ConfigError("work_offsets: \"" + name + "\" is not one of G54..G59");
        m.work_offsets[static_cast<std::size_t>(idx)] = read_xyz(off, "work_offsets." + name, {}, false);
      }
    } else if (key == "tools") {
      if (!v.is_object()) throw ConfigError("tools must be an object like {\"1\": {\"diameter_mm\": 6}}");
      for (auto& [num, t] : v.items()) {
        int n = 0;
        try {
          std::size_t used = 0;
          n = std::stoi(num, &used);
          if (used != num.size() || n <= 0) throw std::invalid_argument("");
        } catch (const std::exception&) {
          throw ConfigError("tools: \"" + num + "\" is not a tool number (1, 2, ...)");
        }
        if (!t.is_object()) throw ConfigError("tools." + num + " must be an object");
        Tool tool;
        for (auto& [tk, tv] : t.items()) {
          if (tk == "diameter_mm")
            tool.diameter_mm = positive(tv, "tools." + num + ".diameter_mm");
          else if (tk == "length_mm")
            tool.length_mm = non_negative(tv, "tools." + num + ".length_mm");
          else if (tk != "comment")
            throw ConfigError("tools." + num + " has unknown key \"" + tk + "\"");
        }
        m.tools[n] = tool;
      }
    }
  }
  return m;
}

MachineConfig load_machine_config(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw ConfigError("cannot open machine config \"" + path + "\"");
  std::stringstream ss;
  ss << in.rdbuf();
  return parse_machine_config(ss.str());
}

}  // namespace gcodesim
