# gcode-sim

A C++20 tool that reads G-code (the instructions a CNC mill follows), checks it for mistakes,
estimates how long it will run, and draws the toolpath.

<p align="center">
  <img src="docs/images/bracket.svg" alt="Toolpath of a rounded bracket with a bore, coloured by feed rate" width="600">
</p>

## Why I built this

I did a co-op as a CAD/CAM programmer, writing and checking G-code for CNC mills every day.
I got curious about what actually happens to that G-code between "export from CAM" and
"the machine starts cutting" — how a controller catches a mistake before it breaks a tool,
how it decides how long a job will take, how it decides how fast it can take a corner. This
project is me building a simplified version of that pipeline myself in C++, partly to actually
understand it and partly to get more comfortable with C++ before some hardware projects
(PID motor control, an FFT vibration analyzer) I'm doing later this year.

## How it was built

The first version of this project — the interpreter, planner, linter, tests and docs — was
built with AI assistance (Claude Code, by Anthropic).

## What it does

- **Reads** a practical subset of G-code: lines, arcs in all three planes, inches/metric,
  absolute/incremental, work offsets, tool length offsets, and drilling cycles
  ([full list](docs/SUPPORTED_GCODE.md)).
- **Lints** with nine rules (rapid into stock, out of travel, missing feed, spindle off while
  cutting, tool change with spindle on, arc radius mismatch, feed above limit, no tool loaded,
  plunging deeper than the tool's diameter in one pass). Every message has a line, column and code.
- **Estimates cycle time** using per-axis speed and acceleration limits, with corner blending
  (look-ahead).
- **Draws** the toolpath as an SVG, coloured by feed rate or depth.
- Bad input produces error messages, never a crash.

## Try it

Needs CMake 3.25+ and a C++20 compiler (developed with GCC 13). Dependencies download automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

./build/gcode-sim stats  examples/bracket.nc -c examples/machine.json
./build/gcode-sim lint   examples/lint_demo.nc -c examples/machine.json
./build/gcode-sim render examples/bracket.nc -c examples/machine.json --svg bracket.svg
```

Exit codes: `0` clean, `1` the program has errors, `2` bad usage or unreadable file/config.

```console
$ gcode-sim lint examples/lint_demo.nc -c examples/machine.json
examples/lint_demo.nc:5:1: warning[LN001]: rapid move ends at Z-4.000 (below Z0); use G1 with a feed rate
examples/lint_demo.nc:6:1: error[LN003]: cutting move with no feed rate (F) set
examples/lint_demo.nc:6:1: error[LN004]: cutting move while the spindle is not running (missing M3/M4?)
examples/lint_demo.nc:8:1: error[LN002]: X reaches 300.000 mm (machine coords), outside travel [-250.000, 250.000]
examples/bracket.nc:8:1: warning[LN009]: cutting move plunges 13.000 mm in Z, deeper than the loaded tool's diameter (6.000 mm)
...
```

## Why look-ahead matters

CAM software exports curves as hundreds of tiny line segments. A machine that stops at every
corner crawls; one that blends corners keeps its speed. On
[examples/contour.nc](examples/contour.nc) (a wavy groove, 200 segments):

| | Cycle time |
|---|---:|
| Stop at every segment (`--no-lookahead`) | 19.2 s |
| With look-ahead | 8.9 s |

(Both include a 5 s tool change from the example machine.)

![Speed with and without look-ahead](docs/images/lookahead.png)

## How it is organised

`text -> Lexer -> Parser -> Interpreter -> list of moves -> Planner / Linter / Stats / SVG`

Only the interpreter understands G-code. Everything after it reads a plain list of moves.
See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md),
or [LEARN.md](LEARN.md) for a guided tour.

## Tests

| | |
|---|---|
| Automated tests | 127 (116 unit/golden cases with 1,721 checks, 11 CLI tests) |
| Line coverage of the library | 97.9% (branches 84.4%) |
| Memory/undefined-behaviour checkers (ASan + UBSan) | suite runs clean |
| Random garbage inputs | 1,000 seeded random programs, all handled without a crash |

Timing is checked against physics (plunge time = distance / feed; a full circle takes
2*pi*r / feed) and against saved "golden" outputs for 14 sample programs.
**It has not been compared with a real machine or CAM software's estimate**, so treat times as
estimates.

Parsing speed on 1,000,000 lines: about 3.7M lines/s for the interpreter, about 1.6x faster than the
simple reference version in [bench/bench_parse.cpp](bench/bench_parse.cpp) (1-core Linux container;
run `build/bench_parse` yourself for your machine's numbers).

## Limitations

- No cutter radius compensation (G41/G42), subprograms, parameters or rotary axes.
- Tight arcs at high feed are timed too optimistically (no centripetal limit).
- M0/M1 pauses and spindle spin-up add no time.

## License

MIT, see [LICENSE](LICENSE).
