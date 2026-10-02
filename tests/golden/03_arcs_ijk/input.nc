(quarter, half and three-quarter arcs, both directions)
G21 G90 G17
T1 M6
S5000 M3
G0 X10 Y0 Z1
G1 Z-1 F100
G3 X0 Y10 I-10 J0 F600
G3 X0 Y-10 I0 J-10
G2 X10 Y0 I0 J10
G0 Z5
M30
