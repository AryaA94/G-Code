// Parse-throughput benchmark: how many lines per second the lexer + parser +
// interpreter get through, compared with a deliberately simple reference
// version that splits lines with std::istringstream and std::stod.
//
//   ./build/bench_parse [lines]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>

#include "gcodesim/interpreter.hpp"

using namespace gcodesim;
using Clock = std::chrono::steady_clock;

namespace {

std::string make_program(int lines) {
  std::string text = "G21 G90 G17\nT1 M6\nS8000 M3\nF1200\n";
  text.reserve(static_cast<std::size_t>(lines) * 24);
  char buf[64];
  for (int i = 0; i < lines; ++i) {
    std::snprintf(buf, sizeof buf, "G1 X%.3f Y%.3f Z%.3f\n", (i % 1000) * 0.1, (i % 37) * 0.25,
                  -(i % 5) * 0.5);
    text += buf;
  }
  return text;
}

// The obvious first version: a stringstream per line, a map per word.
std::size_t reference_parse(const std::string& text) {
  std::istringstream in(text);
  std::string line;
  std::size_t moves = 0;
  double x = 0, y = 0, z = 0;
  while (std::getline(in, line)) {
    std::map<char, double> words;
    for (std::size_t i = 0; i < line.size();) {
      char c = line[i];
      if (std::isalpha(static_cast<unsigned char>(c))) {
        std::size_t used = 0;
        words[static_cast<char>(std::toupper(c))] = std::stod(line.substr(i + 1), &used);
        i += 1 + used;
      } else {
        ++i;
      }
    }
    if (words.count('X')) x = words['X'];
    if (words.count('Y')) y = words['Y'];
    if (words.count('Z')) z = words['Z'];
    if (words.count('G')) ++moves;
  }
  return moves + static_cast<std::size_t>(x + y + z) * 0;
}

template <typename F>
double seconds(F&& f) {
  auto t0 = Clock::now();
  f();
  return std::chrono::duration<double>(Clock::now() - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
  int lines = argc > 1 ? std::atoi(argv[1]) : 1000000;
  std::string text = make_program(lines);
  MachineConfig machine;

  std::size_t segs = 0, ref = 0;
  double t_ref = seconds([&] { ref = reference_parse(text); });
  double t_new = seconds([&] { segs = interpret(text, machine).segments.size(); });

  std::printf("%d lines\n", lines);
  std::printf("reference (stringstream + map):  %.3f s  %.2fM lines/s\n", t_ref, lines / t_ref / 1e6);
  std::printf("gcodesim interpret():            %.3f s  %.2fM lines/s  (%zu segments)\n", t_new,
              lines / t_new / 1e6, segs);
  std::printf("speed-up: %.2fx\n", t_ref / t_new);
  return ref > 0 ? 0 : 1;
}
