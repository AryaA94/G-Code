#!/usr/bin/env python3
"""Writes the generated example programs (contour, pocket, gear) into examples/.

The hand-written ones (bracket, drill_plate, lint_demo) are edited directly.
Run from the repo root: python3 scripts/make_examples.py
"""
import math
from pathlib import Path

EXAMPLES = Path(__file__).resolve().parent.parent / "examples"


def fmt(v):
    s = f"{v:.3f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def contour():
    # A wavy groove made of 200 short lines, the way CAM exports a spline.
    # Good for seeing what look-ahead does: every junction is a tiny corner.
    lines = [
        "(CONTOUR - 100 mm sine-wave groove, 200 short segments like a CAM spline export)",
        "G21 G17 G90 G94",
        "G54",
        "T3 M6",
        "G43 H3",
        "S12000 M3",
        "G0 Z5",
        "G0 X0 Y0",
        "G1 Z-1 F300",
        "F1500",
    ]
    for i in range(1, 201):
        x = i * 0.5
        y = 8.0 * math.sin(2 * math.pi * x / 25.0)
        lines.append(f"G1 X{fmt(x)} Y{fmt(y)}")
    lines += ["G0 Z5", "M5", "G28 G91 Z0", "G90", "M30"]
    return lines


def pocket():
    # Rectangular pocket cleared with shrinking rectangles, 2 mm step-over,
    # two depths. Corners are rounded with arcs so the tool doesn't stop dead.
    lines = [
        "(POCKET - 60 x 40 x 4 mm pocket, concentric passes, 6 mm end mill)",
        "G21 G17 G90 G94",
        "G54",
        "T1 M6",
        "G43 H1",
        "S10000 M3",
        "G0 Z5",
    ]
    x0, y0, w, h, r_tool, step = 20.0, 20.0, 60.0, 40.0, 3.0, 2.5
    for depth in (-2.0, -4.0):
        lines.append(f"(depth {fmt(depth)})")
        cx, cy = x0 + w / 2, y0 + h / 2
        hw, hh = w / 2 - r_tool, h / 2 - r_tool
        rings = []
        while hw > 0 and hh > 0:
            rings.append((hw, hh))
            hw -= step
            hh -= step
        rings.reverse()  # start in the middle and work outwards
        first = True
        for hw, hh in rings:
            c = min(3.0, hw, hh)  # corner radius of this ring
            start = (cx - hw, cy - hh + c)
            if first:
                lines += [f"G0 X{fmt(start[0])} Y{fmt(start[1])}", "G0 Z1", f"G1 Z{fmt(depth)} F250", "F1200"]
                first = False
            else:
                lines.append(f"G1 X{fmt(start[0])} Y{fmt(start[1])}")
            l, rgt, b, t = cx - hw, cx + hw, cy - hh, cy + hh
            lines += [
                f"G1 Y{fmt(t - c)}",
                f"G2 X{fmt(l + c)} Y{fmt(t)} I{fmt(c)} J0",
                f"G1 X{fmt(rgt - c)}",
                f"G2 X{fmt(rgt)} Y{fmt(t - c)} I0 J{fmt(-c)}",
                f"G1 Y{fmt(b + c)}",
                f"G2 X{fmt(rgt - c)} Y{fmt(b)} I{fmt(-c)} J0",
                f"G1 X{fmt(l + c)}",
                f"G2 X{fmt(l)} Y{fmt(b + c)} I0 J{fmt(c)}",
            ]
        lines.append("G0 Z5")
    lines += ["M5", "G28 G91 Z0", "G90", "M30"]
    return lines


def gear():
    # 12-tooth gear outline engraved 0.3 mm deep: straight flanks and arcs
    # on the tip and root circles. Uses G55, the second work offset.
    teeth, r_tip, r_root = 12, 30.0, 25.0
    lines = [
        "(GEAR - 12 tooth outline engraved 0.3 mm deep, on the G55 work offset)",
        "G21 G17 G90 G94",
        "G55",
        "T4 M6",
        "G43 H4",
        "S15000 M3",
        "G0 Z3",
    ]
    pitch = 2 * math.pi / teeth
    pts = []
    for k in range(teeth):
        a = k * pitch
        # root arc from a to a+0.45p, flank up, tip arc to a+0.95p, flank down
        pts.append(("root", a, a + 0.45 * pitch))
        pts.append(("tip", a + 0.5 * pitch, a + 0.9 * pitch))
    x, y = r_root * math.cos(0), r_root * math.sin(0)
    lines += [f"G0 X{fmt(x)} Y{fmt(y)}", "G1 Z-0.3 F200", "F800"]
    for kind, a0, a1 in pts:
        r = r_root if kind == "root" else r_tip
        sx, sy = r * math.cos(a0), r * math.sin(a0)
        ex, ey = r * math.cos(a1), r * math.sin(a1)
        lines.append(f"G1 X{fmt(sx)} Y{fmt(sy)}")
        lines.append(f"G3 X{fmt(ex)} Y{fmt(ey)} I{fmt(-sx)} J{fmt(-sy)}")
    lines += [f"G1 X{fmt(r_root)} Y0", "G0 Z3"]
    # center hole: 8 mm circle
    lines += ["G0 X4 Y0", "G1 Z-0.3 F200", "G3 X4 Y0 I-4 J0 F800", "G0 Z10"]
    lines += ["M5", "G28 G91 Z0", "G90", "M30"]
    return lines


def main():
    for name, fn in (("contour", contour), ("pocket", pocket), ("gear", gear)):
        path = EXAMPLES / f"{name}.nc"
        path.write_text("\n".join(fn()) + "\n")
        print("wrote", path.relative_to(EXAMPLES.parent))


if __name__ == "__main__":
    main()
