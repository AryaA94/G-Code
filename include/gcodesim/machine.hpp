#pragma once

#include <array>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

#include "gcodesim/segment.hpp"

namespace gcodesim {

struct Tool {
  double diameter_mm = 0.0;
  double length_mm = 0.0;
  int flutes = 0;     // 0 = unknown: no chip-load check
  bool hss = false;   // high-speed steel instead of carbide: slower speeds
};

// Everything the simulator needs to know about the machine. Rates are per
// axis, like Grbl's $110-$122 settings.
struct MachineConfig {
  std::string name = "default machine";
  Vec3 max_rate_mm_min{5000.0, 5000.0, 3000.0};  // also the rapid speed
  Vec3 accel_mm_s2{500.0, 500.0, 300.0};
  double max_feed_mm_min = 5000.0;  // LN007 limit for programmed F
  double junction_deviation_mm = 0.01;
  double tool_change_time_s = 5.0;
  // false for machines without an automatic changer (most GRBL routers):
  // the operator swaps the bit by hand, so programs often never say M6
  bool tool_changer = true;
  // key from materials(), e.g. "ar500"; empty = unknown, no speed checks
  std::string stock_material;
  std::array<double, 3> travel_min{-1e9, -1e9, -1e9};
  std::array<double, 3> travel_max{1e9, 1e9, 1e9};
  bool has_travel = false;
  std::array<Vec3, 6> work_offsets{};  // G54..G59
  std::map<int, Tool> tools;
};

class ConfigError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Reads a machine.json. Missing keys keep their defaults; wrong types,
// negative rates and unknown keys throw ConfigError with a readable message.
MachineConfig parse_machine_config(std::string_view json_text);
MachineConfig load_machine_config(const std::string& path);

}  // namespace gcodesim
