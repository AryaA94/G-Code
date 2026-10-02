#pragma once

#include <memory>
#include <string>
#include <vector>

#include "gcodesim/diagnostics.hpp"
#include "gcodesim/machine.hpp"
#include "gcodesim/segment.hpp"

namespace gcodesim {

// A lint rule looks at the finished list of moves and reports anything
// suspicious. Rules never see G-code text, only Segments.
class Rule {
 public:
  virtual ~Rule() = default;
  virtual std::string code() const = 0;     // "LN001"
  virtual std::string summary() const = 0;  // one line for `gcode-sim rules`
  virtual void check(const std::vector<Segment>& segments, const MachineConfig& machine,
                     std::vector<Diagnostic>& out) const = 0;
};

std::vector<std::unique_ptr<Rule>> make_default_rules();

// Runs every rule and returns the diagnostics sorted by line.
std::vector<Diagnostic> lint(const std::vector<Segment>& segments, const MachineConfig& machine);

}  // namespace gcodesim
