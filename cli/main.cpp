// gcode-sim: command-line front end. All real work happens in the library;
// this file only reads arguments and files and picks an output format.
//
// Exit codes: 0 = clean, 1 = the program has errors, 2 = bad usage,
// unreadable file or bad machine config.

#include <CLI/CLI.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

#include "gcodesim/analysis.hpp"
#include "gcodesim/linter.hpp"
#include "gcodesim/report.hpp"
#include "gcodesim/svg.hpp"

#ifndef GCODESIM_VERSION
#define GCODESIM_VERSION "dev"
#endif

using namespace gcodesim;

namespace {

struct Common {
  std::string file;
  std::string config;
  std::string format = "text";
  bool no_lookahead = false;
};

void add_common(CLI::App* cmd, Common& c) {
  cmd->add_option("file", c.file, "G-code program (.nc, .ngc, .gcode)")->required()->check(CLI::ExistingFile);
  cmd->add_option("-c,--config", c.config, "machine config (JSON)")->check(CLI::ExistingFile);
  cmd->add_flag("--no-lookahead", c.no_lookahead, "stop at every junction instead of blending corners");
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw ConfigError("cannot read \"" + path + "\"");
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"gcode-sim: check G-code, estimate cycle time, draw the toolpath"};
  app.set_version_flag("-V,--version", std::string("gcode-sim ") + GCODESIM_VERSION);
  app.require_subcommand(1);

  Common c;
  auto* stats = app.add_subcommand("stats", "cycle time, distances and extents");
  add_common(stats, c);
  stats->add_option("-f,--format", c.format, "text or json")->check(CLI::IsMember({"text", "json"}));

  auto* lint_cmd = app.add_subcommand("lint", "check the program for mistakes");
  add_common(lint_cmd, c);
  lint_cmd->add_option("-f,--format", c.format, "text or json")->check(CLI::IsMember({"text", "json"}));

  SvgOptions svg_opt;
  std::string svg_path, color = "feed";
  bool no_rapids = false;
  auto* render = app.add_subcommand("render", "draw the toolpath (top view) as SVG");
  add_common(render, c);
  render->add_option("--svg", svg_path, "output file")->required();
  render->add_option("--color", color, "colour cutting moves by feed or depth")
      ->check(CLI::IsMember({"feed", "depth"}));
  render->add_option("--width", svg_opt.width, "image width in px")->check(CLI::Range(200, 10000));
  render->add_option("--height", svg_opt.height, "image height in px")->check(CLI::Range(150, 10000));
  render->add_flag("--no-rapids", no_rapids, "leave rapid moves out");

  auto* rules = app.add_subcommand("rules", "list the lint rules");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    int rc = app.exit(e);
    return rc == 0 ? 0 : 2;
  }

  if (rules->parsed()) {
    for (const auto& r : make_default_rules()) std::cout << r->code() << "  " << r->summary() << "\n";
    return 0;
  }

  try {
    MachineConfig machine = c.config.empty() ? MachineConfig{} : load_machine_config(c.config);
    std::string text = read_file(c.file);
    PlannerOptions popt;
    popt.lookahead = !c.no_lookahead;
    Analysis a = analyze(text, machine, popt);
    auto diags = a.diagnostics();
    int rc = has_errors(diags) ? 1 : 0;

    if (stats->parsed()) {
      if (c.format == "json")
        std::cout << stats_json(a).dump(2) << "\n";
      else
        std::cout << stats_text(a, machine);
    } else if (lint_cmd->parsed()) {
      if (c.format == "json")
        std::cout << diagnostics_json(diags).dump(2) << "\n";
      else
        std::cout << diagnostics_text(diags, c.file);
    } else if (render->parsed()) {
      svg_opt.color_by = color == "depth" ? ColorBy::Depth : ColorBy::Feed;
      svg_opt.show_rapids = !no_rapids;
      svg_opt.title = c.file;
      std::ofstream out(svg_path);
      if (!out) {
        std::cerr << "error: cannot write \"" << svg_path << "\"\n";
        return 2;
      }
      out << render_svg(a.program.segments, svg_opt);
      std::cout << "wrote " << svg_path << " (" << a.program.segments.size() << " moves)\n";
      for (const auto& d : diags)
        if (d.severity == Severity::Error) std::cerr << format_diagnostic(d, c.file) << "\n";
    }
    return rc;
  } catch (const ConfigError& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }
}
