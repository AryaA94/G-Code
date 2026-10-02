(LINT DEMO - almost every line below has a mistake on purpose; run: gcode-sim lint)
G21 G90 G17
G0 Z5
G0 X10 Y10
G0 Z-4
G1 X20
G0 Z5
G0 X300
T1 M6
S8000 M3
G1 X30 Y10 Z-2 F500
G1 Z-9 F200
G2 X40 Y10 I5.2 J0 F800
G1 X50 F9000
T3 M6
M5
G1 X60
M30
