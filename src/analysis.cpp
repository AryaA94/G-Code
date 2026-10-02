#include "gcodesim/analysis.hpp"

#include <algorithm>

namespace gcodesim {

std::vector<Diagnostic> Analysis::diagnostics() const {
  std::vector<Diagnostic> all = program.diagnostics;
  all.insert(all.end(), lint.begin(), lint.end());
  std::stable_sort(all.begin(), all.end(),
                   [](const Diagnostic& a, const Diagnostic& b) { return a.line < b.line; });
  return all;
}

Analysis analyze(std::string_view text, const MachineConfig& machine, const PlannerOptions& options) {
  Analysis a;
  a.program = interpret(text, machine);
  a.plan = plan(a.program.segments, machine, options);
  a.lint = lint(a.program.segments, machine);
  a.stats = compute_stats(a.program, a.plan);
  return a;
}

}  // namespace gcodesim
