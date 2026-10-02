(every line has a syntax problem; the good parts must still run)
G21 G90
G0 X10 (unclosed comment
G1 X Y5 F100
G1 X#1
G1 X1 X2
G0 G1 X3
G12 X4
G41 D1
M98 P1
G1 A90
G1 X5 E2
G80
X6
G2 X10 F100
G4
M6
M30
G0 X99
