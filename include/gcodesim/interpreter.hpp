#pragma once

#include <string_view>
#include <vector>

#include "gcodesim/diagnostics.hpp"
#include "gcodesim/machine.hpp"
#include "gcodesim/parser.hpp"
#include "gcodesim/segment.hpp"

namespace gcodesim {

inline constexpr double kMmPerInch = 25.4;

// The result of running a program: a flat list of explicit moves plus every
// problem found on the way. Nothing after this stage knows about G-code.
struct Program {
  std::vector<Segment> segments;
  std::vector<Diagnostic> diagnostics;
  int line_count = 0;
  bool ended = false;  // saw M2 or M30
};

Program interpret(std::string_view text, const MachineConfig& machine);
Program interpret(const std::vector<Block>& blocks, const MachineConfig& machine);

}  // namespace gcodesim
