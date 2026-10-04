#pragma once

#include <string>
#include <vector>

namespace gcodesim {

// Rough cutting data for one stock material: the usual surface-speed range
// for a carbide tool (HSS runs at about a third), and the usual chip load per
// tooth for an end mill, as a fraction of its diameter. These are handbook
// starting points for warnings, not a feeds-and-speeds calculator.
struct Material {
  std::string key;    // used in machine.json: "stock_material": "ar500"
  std::string label;  // shown to people
  double vc_min_m_min, vc_max_m_min;
  double fz_min_per_d, fz_max_per_d;
};

const std::vector<Material>& materials();
const Material* find_material(const std::string& key);

// Surface speed in m/min for a tool of diameter d_mm at rpm.
inline double surface_speed_m_min(double d_mm, double rpm) {
  return 3.141592653589793 * d_mm * rpm / 1000.0;
}

// HSS tools are run at roughly this fraction of the carbide surface speed.
constexpr double kHssSpeedFactor = 0.33;

}  // namespace gcodesim
