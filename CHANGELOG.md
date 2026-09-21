# Changelog

All notable changes are listed here. Format: [Keep a Changelog](https://keepachangelog.com/),
versioning: [SemVer](https://semver.org/).

## [0.1.0] - 2026-09-21

First release.

### Added
- Lexer, parser and modal interpreter for a practical 3-axis milling subset of RS-274 G-code
  (see `docs/SUPPORTED_GCODE.md`): linear and circular moves in all three planes (I/J/K and R
  forms, helical), inch/metric, absolute/incremental, G54-G59, G92, tool length offsets,
  G81/G83 canned cycles.
- Acceleration-aware cycle-time estimate with Grbl-style junction-deviation look-ahead.
- Linter with nine rules (LN001-LN009) and stable diagnostic codes for parse errors (GC001-GC022).
- CLI: `stats`, `lint`, `render` (SVG), `rules`; text and JSON output; documented exit codes.
- Test suite: unit tests, golden-file tests, CLI tests, analytic checks, randomized robustness tests.
- Parse-throughput benchmark, velocity-profile plot script.
- GitHub Actions: Linux (GCC, Clang), macOS, Windows builds; ASan/UBSan; coverage.

### Not supported (by design, reported as diagnostics)
- Cutter radius compensation (G41/G42), inverse-time feed (G93), parameters and expressions
  (`#1`, `[...]`), subprograms, rotary axes.
