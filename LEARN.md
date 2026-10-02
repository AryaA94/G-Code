# Learning guide

Goal: understand this project well enough to explain it in an interview and
change it with confidence. Do the steps in order. Each exercise ends with
something you can commit in your own words.

## 1. Follow one line through the whole program

Take the line `G1 X10 F200`.

| Stage | File | What happens |
|---|---|---|
| Lexer | `src/lexer.cpp` | Splits it into words: `G1`, `X10`, `F200` (letter, number, column) |
| Parser | `src/parser.cpp` | Makes one `Block`: G code 10 (stored in tenths), X = 10, F = 200 |
| Interpreter | `src/interpreter.cpp` | Sets the feed to 200, the motion mode to linear, works out the target point and pushes one `Segment` |
| Planner | `src/planner.cpp` | 10 mm at 200 mm/min = 3.33 mm/s; builds a speed-up / cruise / slow-down profile |
| Linter | `src/linter.cpp` | Checks the segment: feed set? spindle on? inside travel? tool loaded? |
| Stats | `src/stats.cpp` | Adds 10 mm to the cutting length and grows the bounding box |

Read the files in that order. Use the tests as examples: `tests/unit/test_lexer.cpp`
shows what the lexer accepts and rejects, in plain input/output pairs.

Then open the [web version](https://aryaa94.github.io/G-Code/), load the
bracket, and step through it with the slider while reading the program. Watch
the speed chart at the corners of the outline.

## 2. Break it on purpose

Each of these changes was made one at a time, the tests were run, and the
code was put back. Try them yourself and read the failure messages.

| Change | Where | Tests that caught it |
|---|---|---|
| A full circle becomes zero length (`sweep <= kEps` becomes `sweep < -kEps`) | `src/segment.cpp`, `arc_sweep` | 4 arc/planner tests + golden files |
| Inches convert with 25 instead of 25.4 | `include/gcodesim/interpreter.hpp` | 2 unit tests + golden files |
| Acceleration distance forgets the factor 2 | `src/planner.cpp`, `trapezoid` | 3 planner tests + golden files |
| Out-of-travel warning repeats for every move outside (drop `reported[a][side] = true`) | `src/linter.cpp` | 2 LN002 tests + golden files |
| G91 treated as absolute | `src/interpreter.cpp`, `target_of` | the G91 test, the G28 test + golden files |

Every bug is caught by at least one small targeted test *and* by the golden
files. That is why there are two kinds of tests. Write down, in your own
words, what each targeted test is checking.

## 3. Read the rules, then write your own

Read `LN005` (short) and `LN009` in `src/linter.cpp`, and their tests in
`tests/unit/test_linter.cpp`. Each rule is a small class with `code()`,
`summary()` and `check()`.

`LN009` is the worked example. Read it slowly until you could write it again
from memory: it looks up the loaded tool's diameter in the machine config,
measures how far each cutting move goes below Z0 (the air above the part
doesn't count), and warns when that is deeper than the tool is wide. Look at
its four tests: why does each one exist?

Then write **LN010 yourself**, the same way: a class, a test that fails first,
then the code that makes it pass. Ideas, easiest first:
- Two identical moves in a row
- A plunge (pure Z move down) faster than a third of the machine's Z rate
- A `G0` rapid that travels sideways more than N mm close to the stock (0 < Z < 1)
- A spindle speed above the machine's maximum (add `max_spindle_rpm` to the config)

Steps: add the class, register it in `make_default_rules()`, write a test,
make it pass, run `ctest`. Also update the rule count in
`tests/unit/test_linter.cpp`, the table in `docs/SUPPORTED_GCODE.md`, and the
web page text ("All nine lint rules"). If golden files fail, run with
`GCODESIM_UPDATE_GOLDEN=1`, read the diff with `git diff`, and only keep it
if the change is what you meant.

## 4. More exercises

1. Add a golden folder in `tests/golden/` for a program of your own (ideally
   one from your CAM work that isn't confidential).
2. Change `junction_deviation_mm` in `examples/machine.json` (0.001, 0.01,
   0.1) and record the contour's cycle time. Explain the trend.
3. Make `bench_parse` faster. `Segment` is about 200 bytes; which fields
   could shrink? Measure before and after.
4. Compare `stats` for one of your real programs with your CAM software's
   estimate. Write down the gap and your theory for it. That result goes
   straight into the README.
5. Add a feature to the web page: show the current line's text in the
   overlay, or a button that jumps to the next tool change.

## 5. Questions to practise answering out loud

- Why turn G-code into a list of simple moves first?
- What is "modal" state and where does it live?
- How does the planner decide how fast to go around a corner? Why do you need
  both a backward and a forward pass?
- Why does a full circle need special handling?
- How does the same C++ run in the browser? What crosses the boundary?
- How do you know the cycle time is right? (Be honest about what has not been
  validated.)
- What was the hardest bug? (Candidates from building it: LN009 counting air
  as depth; the interpreter storing every parsed line and running 4x slower
  than a naive version.)
- What would you do next?

## 6. Making it yours

The commits should show what you actually did. Do exercises 3 and 4 as
separate small commits with messages in your own words, and rewrite "Why I
built this" in `README.md` in your own voice.
