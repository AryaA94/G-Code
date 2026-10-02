#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "gcodesim/analysis.hpp"

namespace gcodesim {

// "1 h 02 min 05.3 s", "3 min 07.0 s", "12.4 s"
std::string format_duration(double seconds);

std::string stats_text(const Analysis& a, const MachineConfig& machine);
nlohmann::json stats_json(const Analysis& a);

std::string diagnostics_text(const std::vector<Diagnostic>& diags, std::string_view filename);
nlohmann::json diagnostics_json(const std::vector<Diagnostic>& diags);

// Stats + diagnostics; what the golden tests compare against.
nlohmann::json analysis_json(const Analysis& a);

// Every move as a polyline with its timing, for the web viewer.
nlohmann::json toolpath_json(const Analysis& a);

// Rounds to 6 significant digits so saved JSON doesn't change with the last
// bit of floating-point noise between compilers.
double round_sig(double v, int digits = 6);

}  // namespace gcodesim
