"""Draw the default-controls map (keyboard + mouse and controller) as SVG.

    python3 scripts/gen_controls_map.py docs/controls.svg

Keep it in sync with the defaults in src/game/config.cpp and RecompFrontend's input_mapping.cpp.
"""
import sys

W, H = 1200, 860
BG, PANEL, KEY, KEY_EDGE, DIM, TEXT, SUB = "#0e1217", "#151b23", "#1d2530", "#2b3542", "#5b6878", "#e8edf3", "#97a3b3"
CAT = {
    "move": ("#2dd4bf", "Move"),
    "fire": ("#f87171", "Shoot"),
    "aim": ("#fbbf24", "Aim / view"),
    "act": ("#60a5fa", "Action"),
    "cam": ("#c084fc", "Camera"),
    "menu": ("#94a3b8", "Menus"),
}
FONT = "Inter, 'Segoe UI', Helvetica, Arial, sans-serif"

out = []
def add(s): out.append(s)
def esc(s): return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

def text(x, y, s, size=14, fill=TEXT, weight=400, anchor="start", extra=""):
    add(f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" font-weight="{weight}" text-anchor="{anchor}" {extra}>{esc(s)}</text>')

def key(x, y, label, w=46, cat=None, h=46):
    if cat:
        c = CAT[cat][0]
        add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="{c}" fill-opacity="0.16" stroke="{c}" stroke-width="2"/>')
        text(x + w / 2, y + h / 2 + 5, label, 15 if len(label) < 4 else 12, c, 700, "middle")
    else:
        add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="{KEY}" stroke="{KEY_EDGE}"/>')
        text(x + w / 2, y + h / 2 + 5, label, 13 if len(label) < 4 else 11, DIM, 500, "middle")

add(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="{FONT}">')
add(f'<rect width="{W}" height="{H}" rx="18" fill="{BG}"/>')
text(40, 56, "MISSION: IMPOSSIBLE", 26, TEXT, 800, extra='letter-spacing="3"')
text(40, 82, "Default controls · rebind anything in the Controls menu (Esc)", 15, SUB)

# ---------------- Keyboard panel ----------------
px, py, pw, ph = 24, 104, 1152, 384
add(f'<rect x="{px}" y="{py}" width="{pw}" height="{ph}" rx="14" fill="{PANEL}"/>')
text(48, 138, "KEYBOARD + MOUSE", 13, SUB, 700, extra='letter-spacing="2"')

P = 52  # key pitch
kx, ky = 48, 160
bind = {
    "Esc": "menu", "Q": "fire", "W": "move", "A": "move", "S": "move", "D": "move", "E": "aim",
    "V": "cam", "J": "cam", "L": "cam", "I": "cam", "K": "cam",
    "Shift": "act", "Ctrl": "act", "Space": "act", "Enter": "menu",
    "↑": "aim", "↓": "act", "←": "aim", "→": "aim",
}
def row(x, y, keys):
    for k in keys:
        if isinstance(k, tuple):
            label, w = k
        else:
            label, w = k, 46
        if label:
            key(x, y, label, w, bind.get(label))
        x += w + 6
key(kx, ky, "Esc", 46, "menu")
row(kx, ky + P, [("Tab", 70), "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"])
row(kx, ky + 2 * P, [("Caps", 82), "A", "S", "D", "F", "G", "H", "J", "K", "L", ";"])
row(kx, ky + 3 * P, [("Shift", 110), "Z", "X", "C", "V", "B", "N", "M", ",", "."])
row(kx, ky + 4 * P, [("Ctrl", 70), ("Alt", 58), ("Space", 330)])
# Enter and arrows
key(kx + 82 + 52 * 10 + 6, ky + 2 * P, "Enter", 92, "menu")
ax, ay = kx + 590, ky + 3 * P
key(ax + 52, ay, "↑", 46, "aim")
key(ax, ay + P, "←", 46, "aim"); key(ax + 52, ay + P, "↓", 46, "act"); key(ax + 104, ay + P, "→", 46, "aim")

# Mouse
mx, my = 850, 170
add(f'<path d="M{mx} {my + 70} C{mx} {my + 10} {mx + 20} {my} {mx + 60} {my} C{mx + 100} {my} {mx + 120} {my + 10} {mx + 120} {my + 70} '
    f'L{mx + 120} {my + 150} C{mx + 120} {my + 200} {mx + 95} {my + 222} {mx + 60} {my + 222} C{mx + 25} {my + 222} {mx} {my + 200} {mx} {my + 150} Z" '
    f'fill="{KEY}" stroke="{KEY_EDGE}" stroke-width="2"/>')
fc, ac, mc = CAT["fire"][0], CAT["aim"][0], CAT["move"][0]
add(f'<path d="M{mx + 58} {my + 2} C{mx + 22} {my + 4} {mx + 3} {my + 14} {mx + 2} {my + 72} L{mx + 58} {my + 72} Z" fill="{fc}" fill-opacity="0.22" stroke="{fc}" stroke-width="2"/>')
add(f'<path d="M{mx + 62} {my + 2} C{mx + 98} {my + 4} {mx + 117} {my + 14} {mx + 118} {my + 72} L{mx + 62} {my + 72} Z" fill="{ac}" fill-opacity="0.22" stroke="{ac}" stroke-width="2"/>')
add(f'<rect x="{mx + 54}" y="{my + 20}" width="12" height="30" rx="6" fill="{KEY_EDGE}"/>')
text(mx + 30, my + 48, "L", 16, fc, 800, "middle")
text(mx + 90, my + 48, "R", 16, ac, 800, "middle")
# arrows for movement
cx, cy = mx + 60, my + 140
for ux, uy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
    tip = (cx + ux * 42, cy + uy * 42)
    b1 = (cx + ux * 30 - uy * 7, cy + uy * 30 + ux * 7)
    b2 = (cx + ux * 30 + uy * 7, cy + uy * 30 - ux * 7)
    add(f'<path d="M{tip[0]} {tip[1]} L{b1[0]} {b1[1]} L{b2[0]} {b2[1]} Z" fill="{mc}"/>')
add(f'<circle cx="{cx}" cy="{cy}" r="5" fill="{mc}"/>')
# callouts
def callout(x1, y1, x2, y2, title, sub, color, anchor="start"):
    add(f'<path d="M{x1} {y1} L{x2} {y2}" stroke="{color}" stroke-width="1.5" fill="none"/>')
    add(f'<circle cx="{x1}" cy="{y1}" r="3" fill="{color}"/>')
    tx = x2 + (8 if anchor == "start" else -8)
    text(tx, y2 + 1, title, 15, TEXT, 700, anchor)
    text(tx, y2 + 20, sub, 12.5, SUB, 400, anchor)
callout(mx + 112, my + 36, mx + 150, my + 20, "Hold: aim", "first-person view", ac)
callout(mx + 8, my + 36, mx - 30, my + 20, "Shoot", "or punch", fc, "end")
callout(mx + 120, my + 120, mx + 150, my + 140, "Move mouse", "turn Ethan · aim", mc)

# Keyboard legend
lx, ly = 48, 444
legend = [
    ("move", "WASD", "Move"),
    ("fire", "Q", "Shoot"),
    ("aim", "E", "Hold: aim"),
    ("act", "Space", "Action / jump"),
    ("act", "Shift", "Weapons"),
    ("act", "Ctrl  ↓", "Crouch / items"),
    ("aim", "← →", "Sidestep (aiming)"),
    ("aim", "↑", "Zoom (aiming)"),
    ("cam", "J L", "Rotate camera"),
    ("cam", "V", "Camera mode"),
    ("menu", "Enter", "Pause / IMF"),
    ("menu", "Esc", "Settings"),
]
col_w = 186
for i, (cat, k, what) in enumerate(legend):
    c = CAT[cat][0]
    x = lx + (i % 6) * col_w
    y = ly + (i // 6) * 26 - 14
    add(f'<rect x="{x}" y="{y - 12}" width="10" height="10" rx="3" fill="{c}"/>')
    text(x + 18, y - 2, k, 13, c, 700)
    text(x + 74, y - 2, what, 13, TEXT, 500)

# ---------------- Controller panel ----------------
cy0 = 504
add(f'<rect x="{px}" y="{cy0}" width="{pw}" height="{H - cy0 - 24}" rx="14" fill="{PANEL}"/>')
text(48, cy0 + 34, "CONTROLLER", 13, SUB, 700, extra='letter-spacing="2"')
gx, gy = 600, cy0 + 170  # pad center
# body
add(f'<path d="M{gx - 150} {gy - 80} C{gx - 60} {gy - 96} {gx + 60} {gy - 96} {gx + 150} {gy - 80} '
    f'C{gx + 210} {gy - 70} {gx + 236} {gy + 30} {gx + 230} {gy + 92} C{gx + 224} {gy + 140} {gx + 170} {gy + 140} {gx + 140} {gy + 96} '
    f'C{gx + 110} {gy + 54} {gx + 60} {gy + 40} {gx} {gy + 40} C{gx - 60} {gy + 40} {gx - 110} {gy + 54} {gx - 140} {gy + 96} '
    f'C{gx - 170} {gy + 140} {gx - 224} {gy + 140} {gx - 230} {gy + 92} C{gx - 236} {gy + 30} {gx - 210} {gy - 70} {gx - 150} {gy - 80} Z" '
    f'fill="{KEY}" stroke="{KEY_EDGE}" stroke-width="2"/>')
# shoulders / triggers
def pill(x, y, w, h, color, label):
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{h / 2}" fill="{color}" fill-opacity="0.18" stroke="{color}" stroke-width="2"/>')
    text(x + w / 2, y + h / 2 + 5, label, 13, color, 800, "middle")
pill(gx - 190, gy - 128, 80, 22, CAT["fire"][0], "LT")
pill(gx + 110, gy - 128, 80, 22, CAT["aim"][0], "RT")
pill(gx - 186, gy - 102, 72, 18, CAT["cam"][0], "LB")
pill(gx + 114, gy - 102, 72, 18, CAT["act"][0], "RB")
def stick(x, y, color, label):
    add(f'<circle cx="{x}" cy="{y}" r="26" fill="{color}" fill-opacity="0.16" stroke="{color}" stroke-width="2"/>')
    add(f'<circle cx="{x}" cy="{y}" r="15" fill="{KEY_EDGE}"/>')
    text(x, y + 5, label, 12, color, 800, "middle")
stick(gx - 140, gy - 26, CAT["move"][0], "L")
stick(gx + 70, gy + 10, CAT["cam"][0], "R")
# dpad
dc = CAT["cam"][0]
dx_, dy_ = gx - 70, gy + 10
add(f'<path d="M{dx_ - 8} {dy_ - 26} h16 v18 h18 v16 h-18 v18 h-16 v-18 h-18 v-16 h18 Z" fill="{dc}" fill-opacity="0.16" stroke="{dc}" stroke-width="2"/>')
# face buttons
fx, fy = gx + 140, gy - 26
for ox, oy, lab, cat in ((0, 22, "A", "act"), (22, 0, "B", "aim"), (-22, 0, "X", "act"), (0, -22, "Y", "aim")):
    c = CAT[cat][0]
    add(f'<circle cx="{fx + ox}" cy="{fy + oy}" r="12" fill="{c}" fill-opacity="0.18" stroke="{c}" stroke-width="2"/>')
    text(fx + ox, fy + oy + 4.5, lab, 12, c, 800, "middle")
# start
add(f'<rect x="{gx + 4}" y="{gy - 44}" width="26" height="12" rx="6" fill="{CAT["menu"][0]}" fill-opacity="0.5"/>')
add(f'<rect x="{gx - 30}" y="{gy - 44}" width="26" height="12" rx="6" fill="{CAT["menu"][0]}" fill-opacity="0.25"/>')

text(gx - 17, gy - 52, "Back", 11, CAT["menu"][0], 700, "middle")
text(gx + 17, gy - 52, "Start", 11, CAT["menu"][0], 700, "middle")
text(gx, gy + 92, "Start: pause / IMF menu  ·  Back: settings", 13, SUB, 500, "middle")
# callouts left
L = [
    (gx - 190, gy - 117, "LT", "Shoot / punch", "fire"),
    (gx - 186, gy - 93, "LB", "Camera mode", "cam"),
    (gx - 166, gy - 26, "Left stick", "Move", "move"),
    (gx - 96, gy + 10, "D-pad ← →", "Rotate camera", "cam"),
]
R = [
    (gx + 190, gy - 117, "RT (hold)", "Aim · first-person view", "aim"),
    (gx + 186, gy - 93, "RB", "Crouch / items", "act"),
    (gx + 152, gy - 4, "A / X", "Action / jump · Weapons", "act"),
    (gx + 174, gy - 26, "Y / B", "Sidestep while aiming", "aim"),
    (gx + 96, gy + 10, "Right stick", "Turn Ethan (click: zoom)", "cam"),
]
def side_callouts(items, x_text, anchor):
    n = len(items)
    top = cy0 + 64
    step = (H - 24 - top - 26) / max(n - 1, 1)
    for i, (x1, y1, title, sub, cat) in enumerate(items):
        y2 = top + i * step
        c = CAT[cat][0]
        elbow = x_text + (-40 if anchor == "start" else 40)
        add(f'<path d="M{x1} {y1} L{elbow} {y2 + 4} L{x_text} {y2 + 4}" stroke="{c}" stroke-opacity="0.55" stroke-width="1.3" fill="none"/>')
        add(f'<circle cx="{x1}" cy="{y1}" r="3" fill="{c}"/>')
        tx = x_text + (-8 if anchor == "end" else 8)
        text(tx, y2, title, 14, c, 700, anchor)
        text(tx, y2 + 18, sub, 12.5, TEXT, 400, anchor)
side_callouts(L, 300, "end")
side_callouts(R, 900, "start")

add("</svg>")
open(sys.argv[1], "w").write("\n".join(out) + "\n")
