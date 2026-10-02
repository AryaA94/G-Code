#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gcodesim {

enum class Severity { Info, Warning, Error };

// One message about the program. Every stage reports problems this way
// instead of throwing, so a bad line never stops the rest of the file.
struct Diagnostic {
  Severity severity = Severity::Error;
  std::string code;  // "GC003" for parse problems, "LN004" for lint rules
  int line = 0;      // 1-based, 0 = not tied to a line
  int column = 0;    // 1-based, 0 = whole line
  std::string message;
};

std::string_view to_string(Severity s);

// "file.nc:6:1: error[LN003]: cutting move with no feed rate (F) set"
std::string format_diagnostic(const Diagnostic& d, std::string_view filename);

bool has_errors(const std::vector<Diagnostic>& diags);
int count(const std::vector<Diagnostic>& diags, Severity s);

}  // namespace gcodesim
