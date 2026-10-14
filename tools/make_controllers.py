#!/usr/bin/env python3
"""Dibuja las ilustraciones de los mandos del esquema de controles (assets/ui/pad_*.png).

Son mandos genéricos dibujados aquí (sin logotipos ni marcas), con la disposición de un mando de
tipo Xbox (joysticks escalonados) y de uno de tipo PlayStation (joysticks simétricos). Las
coordenadas de cada botón, en píxeles de la imagen de 1200x760, se escriben también en
src/pad_layout.h para que el launcher trace las líneas hasta cada botón.

Requiere: pillow.  Uso: python3 tools/make_controllers.py
"""
import math
import os
from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
W, H = 1200, 760
SS = 3  # supermuestreo para bordes suaves

BODY_TOP = (58, 54, 54)
BODY_BOT = (24, 22, 22)
RIM = (150, 28, 22)
PART = (16, 15, 15)
PART_RIM = (92, 88, 86)
INK = (225, 220, 214)


def s(v):
    return int(round(v * SS))


def body_mask(stick_style):
    m = Image.new("L", (W * SS, H * SS), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle((s(185), s(165), s(1015), s(500)), radius=s(150), fill=255)
    # Empuñaduras
    for cx in (330, 870):
        d.ellipse((s(cx - 165), s(250), s(cx + 165), s(700)), fill=255)
    d.rectangle((s(330), s(330), s(870), s(560)), fill=255)
    # Escotadura inferior entre las empuñaduras
    d.ellipse((s(450), s(520), s(750), s(820)), fill=0)
    return m


def gradient(top, bot):
    g = Image.new("RGB", (W * SS, H * SS))
    d = ImageDraw.Draw(g)
    for y in range(H * SS):
        t = y / (H * SS)
        c = tuple(int(top[i] * (1 - t) + bot[i] * t) for i in range(3))
        d.line((0, y, W * SS, y), fill=c)
    return g


def circle(d, cx, cy, r, fill, outline=None, width=0):
    d.ellipse((s(cx - r), s(cy - r), s(cx + r), s(cy + r)), fill=fill, outline=outline, width=s(width))


def dpad(d, cx, cy, size=58, arm=24):
    for dx, dy, w, h in ((0, 0, arm, size), (0, 0, size, arm)):
        d.rounded_rectangle((s(cx - w), s(cy - h), s(cx + w), s(cy + h)), radius=s(7), fill=PART,
                            outline=PART_RIM, width=s(3))
    d.rectangle((s(cx - arm + 3), s(cy - arm + 3), s(cx + arm - 3), s(cy + arm - 3)), fill=PART)
    for ang in (0, 90, 180, 270):  # flechas en relieve
        a = math.radians(ang)
        px, py = cx + math.cos(a) * 40, cy + math.sin(a) * 40
        tri = [(px + math.cos(a) * 9, py + math.sin(a) * 9),
               (px + math.cos(a + 2.2) * 9, py + math.sin(a + 2.2) * 9),
               (px + math.cos(a - 2.2) * 9, py + math.sin(a - 2.2) * 9)]
        d.polygon([(s(x), s(y)) for x, y in tri], fill=PART_RIM)


def stick(d, cx, cy):
    circle(d, cx, cy, 62, (12, 11, 11), PART_RIM, 3)
    circle(d, cx, cy, 44, (38, 36, 36), (110, 104, 100), 3)
    circle(d, cx, cy, 30, (30, 28, 28))


def face_xbox(d, cx, cy):
    cols = {"Y": (232, 196, 60), "X": (90, 140, 230), "B": (225, 70, 60), "A": (80, 200, 110)}
    pos = {"Y": (0, -62), "X": (-62, 0), "B": (62, 0), "A": (0, 62)}
    anchors = {}
    for k, (dx, dy) in pos.items():
        x, y = cx + dx, cy + dy
        circle(d, x, y, 30, PART, PART_RIM, 3)
        # letra
        f = cols[k]
        letter(d, k, x, y, f)
        anchors[k] = (x, y)
    return anchors


def letter(d, ch, x, y, col):
    w = 4
    if ch == "A":
        pts = [(x - 11, y + 13), (x, y - 14), (x + 11, y + 13)]
        d.line([(s(a), s(b)) for a, b in pts], fill=col, width=s(w), joint="curve")
        d.line((s(x - 6), s(y + 4), s(x + 6), s(y + 4)), fill=col, width=s(w))
    elif ch == "B":
        d.line((s(x - 9), s(y - 13), s(x - 9), s(y + 13)), fill=col, width=s(w))
        d.arc((s(x - 13), s(y - 13), s(x + 9), s(y + 1)), -90, 90, fill=col, width=s(w))
        d.arc((s(x - 13), s(y - 1), s(x + 11), s(y + 13)), -90, 90, fill=col, width=s(w))
        d.line((s(x - 9), s(y - 13), s(x - 2), s(y - 13)), fill=col, width=s(w))
        d.line((s(x - 9), s(y + 13), s(x - 1), s(y + 13)), fill=col, width=s(w))
        d.line((s(x - 9), s(y), s(x - 2), s(y)), fill=col, width=s(w))
    elif ch == "X":
        d.line((s(x - 11), s(y - 13), s(x + 11), s(y + 13)), fill=col, width=s(w))
        d.line((s(x + 11), s(y - 13), s(x - 11), s(y + 13)), fill=col, width=s(w))
    elif ch == "Y":
        d.line((s(x - 11), s(y - 13), s(x), s(y)), fill=col, width=s(w))
        d.line((s(x + 11), s(y - 13), s(x), s(y)), fill=col, width=s(w))
        d.line((s(x), s(y)), (s(x), s(y + 14)), fill=col, width=s(w)) if False else \
            d.line((s(x), s(y), s(x), s(y + 14)), fill=col, width=s(w))


def face_ps(d, cx, cy):
    pos = {"tri": (0, -62), "sq": (-62, 0), "ci": (62, 0), "cr": (0, 62)}
    anchors = {}
    for k, (dx, dy) in pos.items():
        x, y = cx + dx, cy + dy
        circle(d, x, y, 30, PART, PART_RIM, 3)
        w = 4
        if k == "tri":
            c = (80, 200, 150)
            pts = [(x, y - 14), (x + 14, y + 10), (x - 14, y + 10), (x, y - 14)]
            d.line([(s(a), s(b)) for a, b in pts], fill=c, width=s(w), joint="curve")
        elif k == "sq":
            c = (225, 120, 190)
            d.rectangle((s(x - 11), s(y - 11), s(x + 11), s(y + 11)), outline=c, width=s(w))
        elif k == "ci":
            c = (230, 80, 80)
            d.ellipse((s(x - 13), s(y - 13), s(x + 13), s(y + 13)), outline=c, width=s(w))
        else:
            c = (110, 150, 235)
            d.line((s(x - 11), s(y - 11), s(x + 11), s(y + 11)), fill=c, width=s(w))
            d.line((s(x + 11), s(y - 11), s(x - 11), s(y + 11)), fill=c, width=s(w))
        anchors[k] = (x, y)
    return anchors


def shoulders(d, top_label_y):
    # Gatillos (detrás) y botones superiores, vistos desde arriba en perspectiva.
    for cx in (330, 870):
        d.rounded_rectangle((s(cx - 95), s(70), s(cx + 95), s(150)), radius=s(34), fill=(30, 28, 28),
                            outline=PART_RIM, width=s(3))
        d.rounded_rectangle((s(cx - 150), s(150), s(cx + 150), s(196)), radius=s(22), fill=(36, 34, 34),
                            outline=PART_RIM, width=s(3))


def small_button(d, cx, cy, w=30, h=18):
    d.rounded_rectangle((s(cx - w), s(cy - h), s(cx + w), s(cy + h)), radius=s(h), fill=PART, outline=PART_RIM,
                        width=s(3))


def draw(kind):
    img = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    shoulders(d, 0)
    mask = body_mask(kind)
    body = gradient(BODY_TOP, BODY_BOT).convert("RGBA")
    body.putalpha(mask)
    # borde rojo fino
    edge = ImageChops.subtract(mask, mask.filter(ImageFilter.MinFilter(s(3) | 1)))
    rim = Image.new("RGBA", img.size, RIM + (255,))
    rim.putalpha(edge)
    # brillo superior
    shine = Image.new("L", img.size, 0)
    ImageDraw.Draw(shine).rounded_rectangle((s(230), s(180), s(970), s(300)), radius=s(80), fill=40)
    shine = ImageChops.multiply(shine, mask).filter(ImageFilter.GaussianBlur(s(18)))
    img = Image.alpha_composite(img, body)
    white = Image.new("RGBA", img.size, (255, 255, 255, 255))
    white.putalpha(shine)
    img = Image.alpha_composite(img, white)
    img = Image.alpha_composite(img, rim)
    d = ImageDraw.Draw(img)

    a = {}
    if kind == "xbox":
        stick(d, 330, 300); a["LS"] = (330, 300)
        dpad(d, 455, 440); a["DPAD"] = (455, 440)
        stick(d, 745, 440); a["RS"] = (745, 440)
        f = face_xbox(d, 870, 300); a.update(f)
        small_button(d, 525, 300, 22, 16); a["BACK"] = (525, 300)
        small_button(d, 675, 300, 22, 16); a["START"] = (675, 300)
        circle(d, 600, 232, 30, PART, RIM, 3); a["GUIDE"] = (600, 232)
    else:
        dpad(d, 330, 300); a["DPAD"] = (330, 300)
        stick(d, 470, 450); a["LS"] = (470, 450)
        stick(d, 730, 450); a["RS"] = (730, 450)
        f = face_ps(d, 870, 300)
        a.update({"A": f["cr"], "B": f["ci"], "X": f["sq"], "Y": f["tri"]})
        small_button(d, 505, 262, 24, 12); a["BACK"] = (505, 262)
        small_button(d, 695, 262, 24, 12); a["START"] = (695, 262)
        circle(d, 600, 360, 24, PART, RIM, 3); a["GUIDE"] = (600, 360)
    a["LB"] = (330, 173); a["RB"] = (870, 173)
    a["LT"] = (330, 110); a["RT"] = (870, 110)
    out = img.resize((W, H), Image.LANCZOS)
    out.save(os.path.join(ROOT, "assets", "ui", "pad_%s.png" % kind), optimize=True)
    return a


def main():
    layouts = {k: draw(k) for k in ("xbox", "ps")}
    order = ["A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "DPAD", "BACK", "START", "GUIDE"]
    with open(os.path.join(ROOT, "src", "pad_layout.h"), "w") as f:
        f.write("// Generado por tools/make_controllers.py: posición de cada botón en las ilustraciones\n")
        f.write("// assets/ui/pad_xbox.png y pad_ps.png (%dx%d). No editar a mano.\n" % (W, H))
        f.write("#pragma once\n\n")
        f.write("enum class PadPart { %s, Count };\n\n" % ", ".join(order))
        f.write("constexpr float kPadImageW = %d, kPadImageH = %d;\n" % (W, H))
        for k, lay in layouts.items():
            f.write("constexpr float kPad%sAnchors[][2] = {\n" % ("Xbox" if k == "xbox" else "Ps"))
            for name in order:
                x, y = lay[name]
                f.write("  {%d, %d}, // %s\n" % (x, y, name))
            f.write("};\n")
    print("ok")


if __name__ == "__main__":
    main()
