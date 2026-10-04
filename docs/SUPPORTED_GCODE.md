# Supported G-code

A 3-axis milling subset of RS-274/NGC (the LinuxCNC dialect), which is also
what Grbl and most Fanuc-style controls accept for these codes.

## G codes

| Code | Meaning | Notes |
|---|---|---|
| G0 | rapid move | timed at the machine's axis limits |
| G1 | linear move at feed F | |
| G2 / G3 | clockwise / counter-clockwise arc | I/J/K center or R radius; helix when the third axis also moves; full circle when start = end (I/J/K only) |
| G4 P | dwell for P seconds | |
| G17 / G18 / G19 | arc plane XY / ZX / YZ | |
| G20 / G21 | inches / millimetres | applied before F on the same line |
| G28 | go to machine zero, through the given point | without axes: all three axes |
| G40 | cutter compensation off | accepted, does nothing |
| G43 H / G49 | tool length offset on / off | lengths come from the machine config |
| G53 | machine coordinates for this line | with G0 or G1 |
| G54 - G59 | work offsets | from the machine config |
| G80 | cancel drilling cycle | |
| G81 | drill | X Y Z R |
| G82 | drill with dwell | P seconds at the bottom |
| G83 | peck drill | Q peck depth, retracts to R between pecks |
| G73 | high-speed peck drill | Q peck depth, backs off 0.254 mm to break the chip |
| G84 | rigid tap | feeds in and back out at F (F = S x pitch) |
| G85 | bore | feeds in and out |
| G86 | bore, spindle stop | feeds in, rapids out |
| G89 | bore with dwell | feeds in, dwells P seconds, feeds out |
| G90 / G91 | absolute / incremental | |
| G90.1 / G91.1 | absolute / incremental arc centers | incremental is the default |
| G92 | set the current position | |
| G94 | feed per minute | the only feed mode |
| G98 / G99 | drilling retract to initial Z / to R | |

Recognised but **not supported** (error GC010): G41, G42 (cutter
compensation), G87, G88 (back boring, manual boring), G93, G95 (other feed modes),
G38.x (probing).

## M codes

| Code | Meaning |
|---|---|
| M0, M1 | pause (adds no time) |
| M2, M30 | end of program; later lines are ignored |
| M3, M4, M5 | spindle clockwise, counter-clockwise, stop |
| M6 | tool change: Z retracts to machine zero, then the tool from T is loaded |
| M7, M8, M9 | coolant (accepted, not simulated) |

Other M codes are ignored with warning GC011.

## Other words

F feed, S spindle speed, T tool, H length-offset number, P dwell/parameter,
Q peck depth, R radius or retract plane, I/J/K arc center, N line number and
O program number (both ignored). Comments `( ... )` and `; ...`, block delete
`/` and `%` are accepted. Numbers can be written `5`, `5.`, `.5`, `+5`.

Not supported: `#` parameters and `[ ]` expressions (GC004), axes A/B/C/U/V/W
(GC012). Letters that are valid G-code but unused here, such as the E of 3D
printer files, get warning GC023.

## Parser and interpreter diagnostics

| Code | Severity | Meaning |
|---|---|---|
| GC001 | error | comment is never closed |
| GC002 | error | letter without a number |
| GC003 | error | unexpected character |
| GC004 | error | parameters or expressions |
| GC005 | error | number out of range |
| GC006 | error | same letter twice on a line |
| GC007 | error | badly formed G or M number |
| GC008 | error | two codes from the same modal group on a line |
| GC009 | error | unknown G code |
| GC010 | error | known G code that isn't supported |
| GC011 | warning | M code that isn't simulated |
| GC012 | error | rotary or secondary axis |
| GC013 | error | axis words with no motion mode (after G80) |
| GC014 | error | arc with neither a center nor a radius |
| GC015 | error | arc radius too small to reach the end point |
| GC016 | error | R-form full circle |
| GC017 | error | G4 without P |
| GC018 | error | drilling cycle without R or Z |
| GC019 | error | G73 or G83 without a usable Q |
| GC020 | error | drilling cycle in G91 |
| GC021 | warning | M6 with no tool selected |
| GC022 | warning | G43 for a tool that isn't in the machine config |
| GC023 | warning | letter that isn't used |
| GC024 | info | lines after M2/M30 |
| GC025 | error | arc with zero radius |

## Lint rules

| Code | Severity | Rule |
|---|---|---|
| LN001 | warning | rapid move goes below Z0, into the stock (drilling cycles excepted) |
| LN002 | error | move leaves the machine's travel, in machine coordinates; once per axis and side |
| LN003 | error | cutting move before any F |
| LN004 | error | cutting with the spindle stopped or at S0; once per stretch |
| LN005 | warning | M6 with the spindle on |
| LN006 | error | arc end is not on the arc's circle (tolerance 0.005 mm or 0.1% of the radius) |
| LN007 | warning | F above the machine's maximum feed; once per value |
| LN008 | warning | cutting before any tool is loaded |
| LN009 | warning | a cutting move goes deeper into the stock than the tool's diameter |
