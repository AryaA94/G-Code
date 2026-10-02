# Changelog

All notable changes are listed here. Format: [Keep a Changelog](https://keepachangelog.com/),
versioning: [SemVer](https://semver.org/).

## [0.1.0] - 2026-09-28

First release.

### Added
- Lexer, parser and modal interpreter for a 3-axis milling subset of RS-274 G-code
  (see `docs/SUPPORTED_GCODE.md`): lines and arcs in all three planes (I/J/K and R forms,
  helixes), inch/metric, absolute/incremental, G54-G59, G92, G53, G28, tool length offsets,
  G81/G82/G83 drilling cycles.
- Cycle-time estimate with per-axis rate and acceleration limits and Grbl-style
  junction-deviation look-ahead.
- Linter with nine rules (LN001-LN009); stable codes for parser and interpreter problems
  (GC001-GC025).
- CLI: `stats`, `lint`, `render` (SVG), `rules`; text and JSON output; documented exit codes.
- Web version: the library compiled to WebAssembly, with a 3D toolpath view, playback,
  diagnostics that jump to their line, and the planned speed over time.
- Tests: unit, golden-file, CLI exit-code, randomized robustness, and an end-to-end test
  of the web page against the CLI. Parse benchmark.
- GitHub Actions: Linux (GCC, Clang), macOS, Windows, ASan/UBSan, coverage, Pages deploy.

### Not supported (reported as diagnostics)
- Cutter radius compensation (G41/G42), inverse-time feed (G93), parameters and
  expressions (`#1`, `[...]`), subprograms, rotary axes.
