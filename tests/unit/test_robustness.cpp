// Random and hostile input: the goal is "never crash, never hang, always
// return diagnostics", not any particular output.
#include <random>

#include "gcodesim/report.hpp"
#include "gcodesim/svg.hpp"
#include "helpers.hpp"

using namespace test;

namespace {

std::string random_program(std::mt19937& rng, int lines) {
  static const std::string letters = "GMXYZIJKRFSTPQHN";
  static const int gs[] = {0,  1,  2,  3,  4,  17, 18, 19, 20, 21, 28, 43, 49,
                           53, 54, 55, 80, 81, 82, 83, 90, 91, 92, 98, 99};
  std::uniform_int_distribution<int> n_words(0, 6), pick(0, 99);
  std::uniform_real_distribution<double> val(-200, 200);
  std::string out;
  for (int l = 0; l < lines; ++l) {
    int words = n_words(rng);
    for (int w = 0; w < words; ++w) {
      int r = pick(rng);
      if (r < 30) {
        out += "G" + std::to_string(gs[static_cast<std::size_t>(pick(rng)) % std::size(gs)]);
      } else if (r < 35) {
        out += "M" + std::to_string(pick(rng) % 10);
      } else if (r < 38) {
        out += static_cast<char>(33 + pick(rng) % 90);  // junk character
      } else {
        out += letters[static_cast<std::size_t>(pick(rng)) % letters.size()];
        out += std::to_string(val(rng));
      }
      out += ' ';
    }
    out += '\n';
  }
  return out;
}

}  // namespace

TEST_CASE("1000 seeded random programs never crash") {
  MachineConfig m;
  m.tools[1] = {6, 50};
  m.has_travel = true;
  for (unsigned seed = 1; seed <= 1000; ++seed) {
    std::mt19937 rng(seed);
    std::string text = random_program(rng, 30);
    Analysis a = analyze(text, m);
    CHECK(std::isfinite(a.plan.total_time_s));
    CHECK(a.plan.total_time_s >= 0.0);
    render_svg(a.program.segments);
    (void)analysis_json(a).dump();
  }
}

TEST_CASE("hostile inputs") {
  MachineConfig m;
  const char* cases[] = {
      "",
      "\n\n\n",
      "((((",
      ")))",
      "G",
      "G1 X",
      "X1e9999",
      "G1 X1.2.3",
      "G1 X- F",
      "G2 X0 Y0 I0 J0 F1",
      "G2 X1000000 Y0 R0.001 F1",
      "G3 I1e-12 F100",
      "G1 X1e15 F1e-15",
      "G83 X0 Z-100000 R0 Q0.0001 F100",
      "G4 P-5",
      "G0 X99999999999999999999",
      "\x01\x02\x7f\xff\xfe",
      "G1 X1 F100\r\nG1 X2\r\n",
  };
  for (const char* c : cases) {
    INFO(c);
    Analysis a = analyze(c, m);
    CHECK(std::isfinite(a.plan.total_time_s));
  }
}

TEST_CASE("long programs stay linear-ish: 50k lines analyze quickly") {
  std::string text = "G21 G90\nF1000\n";
  for (int i = 0; i < 50000; ++i)
    text += "G1 X" + std::to_string(i % 100) + " Y" + std::to_string(i % 7) + "\n";
  MachineConfig m;
  auto a = analyze(text, m);
  CHECK(a.stats.linear_moves == 50000);
}
