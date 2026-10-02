#include "gcodesim/diagnostics.hpp"

#include <algorithm>

namespace gcodesim {

std::string_view to_string(Severity s) {
  switch (s) {
    case Severity::Info:
      return "info";
    case Severity::Warning:
      return "warning";
    case Severity::Error:
      return "error";
  }
  return "error";
}

std::string format_diagnostic(const Diagnostic& d, std::string_view filename) {
  std::string out(filename);
  out += ':' + std::to_string(d.line) + ':' + std::to_string(std::max(d.column, 1)) + ": ";
  out += to_string(d.severity);
  out += '[' + d.code + "]: " + d.message;
  return out;
}

bool has_errors(const std::vector<Diagnostic>& diags) {
  return count(diags, Severity::Error) > 0;
}

int count(const std::vector<Diagnostic>& diags, Severity s) {
  return static_cast<int>(
      std::count_if(diags.begin(), diags.end(), [s](const Diagnostic& d) { return d.severity == s; }));
}

}  // namespace gcodesim
