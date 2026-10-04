#include "gcodesim/parser.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace gcodesim {

namespace {

std::string g_name(int tenths) {
  std::string s = "G" + std::to_string(tenths / 10);
  if (tenths % 10 != 0) s += "." + std::to_string(tenths % 10);
  return s;
}

// Groups follow the NIST RS-274/NGC interpreter spec, table 4.
// 0 = non-modal, 1 = motion, 2 = plane, 3 = distance, 5 = feed mode,
// 6 = units, 8 = tool length offset, 10 = canned return, 12 = work offset,
// 13 = arc distance.
constexpr struct {
  int tenths;
  int group;
} kGCodes[] = {
    {0, 1},
    {10, 1},
    {20, 1},
    {30, 1},
    {40, 0},
    {170, 2},
    {180, 2},
    {190, 2},
    {200, 6},
    {210, 6},
    {280, 0},
    {530, 0},
    {800, 1},
    {810, 1},
    {820, 1},
    {830, 1},
    {430, 8},
    {490, 8},
    {540, 12},
    {550, 12},
    {560, 12},
    {570, 12},
    {580, 12},
    {590, 12},
    {900, 3},
    {910, 3},
    {901, 13},
    {911, 13},
    {920, 0},
    {940, 5},
    {980, 10},
    {990, 10},
    // recognised but unsupported, see is_unsupported_g()
    {400, 7},
    {410, 7},
    {420, 7},
    {930, 5},
    {950, 5},
    {730, 1},
    {840, 1},
    {850, 1},
    {860, 1},
    {870, 1},
    {880, 1},
    {890, 1},
    {382, 1},
    {383, 1},
    {384, 1},
    {385, 1},
};

constexpr int kSupportedM[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 30};

int m_modal_group(int m) {
  switch (m) {
    case 0:
    case 1:
    case 2:
    case 30:
      return 4;  // stopping
    case 3:
    case 4:
    case 5:
      return 7;  // spindle
    case 6:
      return 6;  // tool change
    case 7:
    case 8:
    case 9:
      return 8;  // coolant
    default:
      return -1;
  }
}

}  // namespace

bool Block::has_g(int tenths) const {
  return std::find(g_codes.begin(), g_codes.end(), tenths) != g_codes.end();
}

bool Block::has_m(int code) const {
  return std::find(m_codes.begin(), m_codes.end(), code) != m_codes.end();
}

bool Block::empty() const {
  return g_codes.empty() && m_codes.empty() &&
         std::none_of(values.begin(), values.end(), [](const auto& v) { return v.has_value(); });
}

int g_modal_group(int tenths) {
  for (const auto& g : kGCodes)
    if (g.tenths == tenths) return g.group;
  return -1;
}

bool is_unsupported_g(int tenths) {
  switch (tenths) {
    case 400:
      return false;  // G40 (compensation off) is harmless, accept it
    case 410:
    case 420:
    case 930:
    case 950:
    case 870:
    case 880:
    case 382:
    case 383:
    case 384:
    case 385:
      return true;
    default:
      return false;
  }
}

Block parse_block(const LexedLine& lexed, std::vector<Diagnostic>& diags) {
  Block b;
  b.line = lexed.line;
  b.block_delete = lexed.block_delete;

  auto report = [&](Severity sev, std::string code, int col, std::string msg) {
    diags.push_back({sev, std::move(code), lexed.line, col, std::move(msg)});
  };

  std::array<int, 16> g_group_col{};  // column of the code already seen in each group
  std::array<int, 16> m_group_col{};

  for (const Word& w : lexed.words) {
    if (w.letter == 'G') {
      double scaled = w.value * 10.0;
      int tenths = static_cast<int>(std::lround(scaled));
      if (w.value < 0 || std::abs(scaled - tenths) > 1e-6) {
        report(Severity::Error, "GC007", w.column, "G-code number must have at most one decimal place");
        continue;
      }
      int group = g_modal_group(tenths);
      if (group < 0) {
        report(Severity::Error, "GC009", w.column, "unknown G-code " + g_name(tenths));
        continue;
      }
      if (is_unsupported_g(tenths)) {
        report(Severity::Error, "GC010", w.column, g_name(tenths) + " is not supported by this simulator");
        continue;
      }
      if (group > 0) {
        if (g_group_col[group] != 0) {
          report(Severity::Error, "GC008", w.column,
                 g_name(tenths) + " conflicts with another G-code from the same group on this line");
          continue;
        }
        g_group_col[group] = w.column;
      }
      b.g_codes.push_back(tenths);
    } else if (w.letter == 'M') {
      int m = static_cast<int>(std::lround(w.value));
      if (w.value < 0 || std::abs(w.value - m) > 1e-9) {
        report(Severity::Error, "GC007", w.column, "M-code number must be a whole number");
        continue;
      }
      if (std::find(std::begin(kSupportedM), std::end(kSupportedM), m) == std::end(kSupportedM)) {
        report(Severity::Warning, "GC011", w.column,
               "M" + std::to_string(m) + " is not simulated and was ignored");
        continue;
      }
      int group = m_modal_group(m);
      if (m_group_col[group] != 0) {
        report(Severity::Error, "GC008", w.column,
               "M" + std::to_string(m) + " conflicts with another M-code from the same group on this line");
        continue;
      }
      m_group_col[group] = w.column;
      b.m_codes.push_back(m);
    } else if (w.letter == 'N' || w.letter == 'O') {
      // line numbers and program numbers carry no meaning for the simulation
    } else if (std::string_view("ABCUVW").find(w.letter) != std::string_view::npos) {
      report(Severity::Error, "GC012", w.column,
             std::string("axis ") + w.letter + " is not supported (3-axis X/Y/Z only)");
    } else {
      int idx = w.letter - 'A';
      if (b.values[idx].has_value()) {
        report(Severity::Error, "GC006", w.column,
               std::string("'") + w.letter + "' appears more than once on this line");
        continue;
      }
      b.values[idx] = w.value;
      b.columns[idx] = w.column;
    }
  }
  return b;
}

std::vector<Block> parse_program(std::string_view text, std::vector<Diagnostic>& diags) {
  std::vector<Block> blocks;
  int line_no = 0;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    std::size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    ++line_no;
    LexedLine lexed = lex_line(text.substr(pos, end - pos), line_no, diags);
    Block b = parse_block(lexed, diags);
    if (!b.empty()) blocks.push_back(std::move(b));
    if (end == text.size()) break;
    pos = end + 1;
  }
  return blocks;
}

}  // namespace gcodesim
