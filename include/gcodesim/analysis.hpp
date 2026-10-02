#pragma once

#include <string_view>
#include <vector>

#include "gcodesim/interpreter.hpp"
#include "gcodesim/linter.hpp"
#include "gcodesim/planner.hpp"
#include "gcodesim/stats.hpp"

namespace gcodesim {

// The whole pipeline in one call: interpret, plan, lint, collect stats.
struct Analysis {
  Program program;
  PlanResult plan;
  std::vector<Diagnostic> lint;
  Stats stats;

  // Interpreter and lint messages together, sorted by line.
  std::vector<Diagnostic> diagnostics() const;
};

Analysis analyze(std::string_view text, const MachineConfig& machine, const PlannerOptions& options = {});

}  // namespace gcodesim
