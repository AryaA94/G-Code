(R-form arcs: short arc with R+, long arc with R-)
G21 G90 G17
T1 M6
S5000 M3
G0 X0 Y0 Z1
G1 Z-1 F100
G2 X20 Y0 R15 F600
G2 X0 Y0 R-15
G3 X20 Y0 R10
G0 Z5
M30
