#pragma once

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "gcodesim/analysis.hpp"
#include "gcodesim/interpreter.hpp"

namespace test {

using namespace gcodesim;
using Catch::Approx;

// Only the moves (rapid/linear/arc), which is what most tests care about.
inline std::vector<Segment> moves(const Program& p) {
  std::vector<Segment> out;
  for (const auto& s : p.segments)
    if (s.is_motion()) out.push_back(s);
  return out;
}

inline Program run(std::string_view text, const MachineConfig& m = {}) {
  return interpret(text, m);
}

inline bool has_code(const std::vector<Diagnostic>& diags, std::string_view code) {
  for (const auto& d : diags)
    if (d.code == code) return true;
  return false;
}

inline int count_code(const std::vector<Diagnostic>& diags, std::string_view code) {
  int n = 0;
  for (const auto& d : diags)
    if (d.code == code) ++n;
  return n;
}

inline void check_point(Vec3 p, double x, double y, double z) {
  CHECK(p.x == Approx(x).margin(1e-9));
  CHECK(p.y == Approx(y).margin(1e-9));
  CHECK(p.z == Approx(z).margin(1e-9));
}

}  // namespace test
