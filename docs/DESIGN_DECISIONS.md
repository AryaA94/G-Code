# Design decisions

Short notes on choices that aren't obvious from the code.

**1. A flat list of moves in the middle.** The interpreter is the only code
that knows G-code. The planner, linter, stats and renderer read `Segment`s.
New rules and outputs don't need to know about modal state, and the
interpreter can be tested on its own by looking at the moves it produces.

**2. Diagnostics instead of exceptions for the program.** A real file often
has one bad line; stopping at it would hide everything after it. Each stage
records a problem and keeps going. Codes are stable (GC0xx, LN0xx) so scripts
and tests can rely on them.

**3. G codes stored in tenths.** `G91.1` and `G38.2` are real codes. Comparing
doubles for equality is fragile, so `G91.1` becomes the integer 911.

**4. Units converted immediately.** Every value becomes mm when a line runs.
Nothing downstream has to know whether the program was in inches.

**5. Work coordinates in Segments, plus an offset.** Drawing and "below Z0"
checks want work coordinates; travel limits want machine coordinates. Keeping
the tool tip in work coordinates and storing the offset gives both without
converting back and forth.

**6. The machine starts at machine zero; M6 retracts first.** Real machines
start homed and lift Z to change tools. Modelling that makes timing more
honest and stops the length offset of a new tool from putting the tip "inside"
the part on paper.

**7. Arcs keep a signed sweep; full circles are 2*pi, never 0.** Start == end
means a full circle. Computing the sweep once, in the interpreter, keeps that
rule in one place (`arc_sweep`).

**8. Junction deviation for corner speed.** It's what Grbl uses, needs one
parameter, and behaves well for both sharp corners and the tiny angles of a
CAM spline.

**9. Lint rules report once where repeats add nothing.** One wrong offset can
make hundreds of moves leave the travel; one message per axis is more useful.
Missing feed and spindle-off are reported once per stretch.

**10. LN009 only counts depth below Z0.** A plunge from Z5 to Z-5 goes 10 mm
but only cuts 5. Counting the air made every normal entry move a warning.
Drilling cycles are skipped: going deep is what a drill is for.

**11. The machine config is strict.** Unknown keys are errors, not ignored,
because a typo like `"acel_mm_s2"` would otherwise silently use the defaults.

**12. Golden files plus targeted tests.** Unit tests say *what* should hold;
the golden files catch *any* change in output, including ones nobody thought
to test. Numbers are rounded to 6 significant digits when saved so they are
stable across compilers.

**13. The web page runs the same library.** It is compiled with Emscripten and
called through three functions that exchange JSON strings. The page draws;
it doesn't simulate. Its end-to-end test compares its results with the native
CLI on every example.
