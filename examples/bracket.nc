%
(BRACKET - 80 x 50 x 6 mm plate, 8 mm corner radius, 20 mm bore, two 5 mm holes)
(Work zero: lower-left corner of the part, top of stock. T1 = 6 mm end mill, T2 = 5 mm drill)
G21 G17 G90 G94
G54
T1 M6
G43 H1
S9000 M3
G0 Z10

(outside profile, two 3 mm passes, climb milling, path offset 3 mm for the tool radius)
G0 X-3 Y8
G0 Z2
G1 Z-3 F300
G1 Y42 F900
G2 X8 Y53 I11 J0
G1 X72
G2 X83 Y42 I0 J-11
G1 Y8
G2 X72 Y-3 I-11 J0
G1 X8
G2 X-3 Y8 I0 J11
G1 Z-6 F300
G1 Y42 F900
G2 X8 Y53 I11 J0
G1 X72
G2 X83 Y42 I0 J-11
G1 Y8
G2 X72 Y-3 I-11 J0
G1 X8
G2 X-3 Y8 I0 J11
G0 Z10

(20 mm bore: helical ramp down 1.5 mm per turn, then one flat pass to clean the floor)
G0 X47 Y25
G0 Z2
G1 Z0 F300
G3 X47 Y25 I-7 J0 Z-1.5 F700
G3 X47 Y25 I-7 J0 Z-3
G3 X47 Y25 I-7 J0 Z-4.5
G3 X47 Y25 I-7 J0 Z-6
G3 X47 Y25 I-7 J0
G1 X40 Y25
G0 Z10
M5

(two 5 mm through holes)
T2 M6
G43 H2
S2500 M3
G0 X12 Y25
G99 G81 X12 Y25 Z-8 R2 F150
X68
G80
G0 Z10
M5
G91 G28 Z0
G90
M30
%
