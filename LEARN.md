# Learning guide

Goal: understand this project well enough to explain it and change it. Do the steps in order.
Each exercise ends with something you can commit in your own words.

## 1. Follow one line through the whole program

Take the line `G1 X10 F200`.

| Stage | File | What happens |
|---|---|---|
| Lexer | `src/lexer.cpp` | Splits it into words: `G1`, `X10`, `F200` (letter, number, column) |
| Parser | `src/parser.cpp` | Makes one `Block`: G-code 10 (stored in tenths), X = 10, F = 200 |
| Interpreter | `src/interpreter.cpp` | Sets motion mode to linear, feed to 200, works out the target point, and pushes one `Segment` onto the list |
| Planner | `src/planner.cpp` | 10 mm at 200 mm/min = 3.33 mm/s; builds a speed-up / cruise / slow-down profile |
| Linter | `src/linter.cpp` | Checks the segment: feed set? spindle on? inside travel? tool loaded? |
| Stats | `src/stats.cpp` | Adds 10 mm to the cutting length and grows the bounding box |

Read the files in that order. Use the tests as examples: `tests/unit/test_lexer.cpp` shows what the
lexer accepts and rejects, in plain input/output pairs.

## 2. Break it on purpose

I made these five changes one at a time, ran the tests, and put the code back. Try them yourself
and see the failure messages.

| Change | Where | Tests that caught it |
|---|---|---|
| A full circle becomes zero length (`sweep <= eps` becomes `sweep < -eps`) | `src/segment.cpp` | 3 arc tests + golden files |
| Inches convert with 25 instead of 25.4 | `include/gcodesim/interpreter.hpp` | 2 modal tests + golden files |
| Acceleration distance forgets the factor 2 | `src/planner.cpp` | 2 trapezoid tests + golden files |
| Out-of-travel warning repeats for every move outside | `src/linter.cpp` | 1 linter test + golden files |
| G91 (incremental) treated as absolute | `src/interpreter.cpp` | the G90/G91 test + golden files |

Notice that every bug is caught by at least one small targeted test *and* by the golden files.
That is why there are two kinds of tests. Write down, in your own words, what each targeted
test is checking.

## 3. Read the rules, then write your own

Read `LN005` (short) and `LN008` / `LN009` in `src/linter.cpp`, and their tests at the bottom of
`tests/unit/test_linter.cpp`. Each rule is a small class that loops over the segment list.

`LN009` was written as a worked example (with help) because of a deadline — read it slowly until
you could redraw it from memory: it takes the loaded tool's diameter from the machine config,
looks at how far each cutting move drops in Z, and warns when that is deeper than the tool is
wide (a real rule of thumb from milling: too much depth in one pass overloads the tool).

Then write **LN010 yourself**, the same way: a class, a test that fails first, then passes.
Ideas, easiest first:
- Spindle speed of 0 while cutting with M3 on (partly covered by LN004; find the gap)
- A plunge (pure Z move down) faster than 1/3 of the machine's Z feed limit
- Two identical moves in a row
- A `G0` rapid that travels more than N mm at cutting depth

Steps: add the class, register it in `make_default_rules()`, write a test that fails first, make it pass,
run `ctest`. If golden files fail, run with `GCODESIM_UPDATE_GOLDEN=1`, read the diff, and only keep
it if the change is what you intended. Also update the rule table in `docs/SUPPORTED_GCODE.md`.

## 4. More exercises

1. Add a new golden folder in `tests/golden/` for a program of your own (ideally one from your CAM work
   that isn't confidential) and regenerate its `expected.json`.
2. Change `junction_deviation` in `examples/machine.json` (0.001, 0.01, 0.1) and record how the
   contour cycle time changes. Explain why.
3. Add a `--width` option to `render` so you can change the SVG size (`cli/main.cpp`, `SvgOptions`).
4. Compare `stats` output for one of your real programs with your CAM software's time estimate.
   Write down the gap and your theory for it. That result goes straight into the README.

## 5. Questions to practise answering out loud

- Why turn G-code into a list of simple moves first?
- What is "modal" state and where does it live?
- How does the planner decide how fast to go around a corner?
- Why does a full circle need special handling?
- How do you know the cycle time is right? (Be honest about what has not been validated.)
- What was the hardest bug? What did you change after finding it?
- What would you do next?

## 6. Making it yours

The commits in your repository should reflect what you actually did. Do exercises 3 and 4 as
separate small commits with messages in your own words, and fill in the two comment blocks in
`README.md` ("Why I built this" and "How it was built").
