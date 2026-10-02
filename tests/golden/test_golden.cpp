// Golden-file tests: each folder in tests/golden/ holds an input.nc (and
// optionally a machine.json). The full analysis is compared with the saved
// expected.json. Any change in output fails the test, so a refactor can't
// quietly change a result.
//
// To accept a deliberate change: GCODESIM_UPDATE_GOLDEN=1 ctest, then read
// the diff in git before committing it.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "gcodesim/analysis.hpp"
#include "gcodesim/report.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace gcodesim;

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Equal, allowing numbers to differ in the last saved digit.
bool close_enough(const json& a, const json& b, std::string path, std::string& why) {
  if (a.is_number() && b.is_number()) {
    double x = a.get<double>(), y = b.get<double>();
    if (std::abs(x - y) <= 1e-5 * std::max({1.0, std::abs(x), std::abs(y)})) return true;
    why = path + ": " + a.dump() + " != " + b.dump();
    return false;
  }
  if (a.type() != b.type()) {
    why = path + ": " + a.dump() + " != " + b.dump();
    return false;
  }
  if (a.is_object()) {
    if (a.size() != b.size()) {
      why = path + ": different keys";
      return false;
    }
    for (auto& [k, v] : a.items()) {
      if (!b.contains(k)) {
        why = path + "." + k + ": missing";
        return false;
      }
      if (!close_enough(v, b[k], path + "." + k, why)) return false;
    }
    return true;
  }
  if (a.is_array()) {
    if (a.size() != b.size()) {
      why = path + ": length " + std::to_string(a.size()) + " != " + std::to_string(b.size());
      return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
      if (!close_enough(a[i], b[i], path + "[" + std::to_string(i) + "]", why)) return false;
    return true;
  }
  if (a != b) {
    why = path + ": " + a.dump() + " != " + b.dump();
    return false;
  }
  return true;
}

}  // namespace

TEST_CASE("golden files") {
  const fs::path root = GCODESIM_GOLDEN_DIR;
  const bool update = std::getenv("GCODESIM_UPDATE_GOLDEN") != nullptr;
  int cases = 0;

  std::vector<fs::path> dirs;
  for (const auto& e : fs::directory_iterator(root))
    if (e.is_directory()) dirs.push_back(e.path());
  std::sort(dirs.begin(), dirs.end());

  for (const auto& dir : dirs) {
    if (!fs::exists(dir / "input.nc")) continue;
    ++cases;
    DYNAMIC_SECTION(dir.filename().string()) {
      MachineConfig machine;
      if (fs::exists(dir / "machine.json")) machine = parse_machine_config(slurp(dir / "machine.json"));
      json actual = analysis_json(analyze(slurp(dir / "input.nc"), machine));

      const fs::path expected_path = dir / "expected.json";
      if (update || !fs::exists(expected_path)) {
        std::ofstream(expected_path) << actual.dump(2) << "\n";
        WARN("wrote " << expected_path.string());
      } else {
        json expected = json::parse(slurp(expected_path));
        std::string why;
        bool same = close_enough(actual, expected, "$", why);
        INFO(why);
        CHECK(same);
      }
    }
  }
  CHECK(cases >= 14);
}
