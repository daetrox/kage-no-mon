#!/usr/bin/env python3
"""Genera los recursos de la interfaz a partir de tools/source.png (la imagen de ejemplo).

Requiere: opencv-python-headless, numpy, pillow.
Uso: python3 tools/make_assets.py
"""
import os
import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "ui")
src = cv2.imread(os.path.join(HERE, "source.png"), cv2.IMREAD_COLOR)
H, W = src.shape[:2]
assert (W, H) == (1536, 1024), (W, H)

# Rectángulos de los botones en la imagen original (x0, y0, x1, y1).
JUGAR = (66, 388, 528, 478)
DARK = [(72, 488, 500, 572), (72, 586, 496, 670), (72, 686, 496, 774)]  # LOGROS, OPCIONES, SALIR

def lum(img):
    return cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)

# --- 1) Sprite rojo (botón seleccionado) con alfa derivada del "rojo" del trazo ---
x0, y0, x1, y1 = JUGAR
red = src[y0:y1, x0:x1].copy()
b, g, r = [red[:, :, i].astype(np.float32) for i in range(3)]
# quitar icono/texto/flecha (píxeles claros y poco saturados) por inpainting
txt = ((lum(red) > 150) & (r - b < 90)).astype(np.uint8) * 255
txt[:, :30] = 0
txt[:, -20:] = 0
txt = cv2.dilate(txt, np.ones((5, 5), np.uint8), iterations=2)
red_clean = cv2.inpaint(red, txt, 6, cv2.INPAINT_TELEA)
b, g, r = [red_clean[:, :, i].astype(np.float32) for i in range(3)]
redness = np.clip((r - np.maximum(g, b) - 12) / 45.0, 0, 1)
# cuerpo del trazo macizo: umbral + cierre morfológico + relleno de huecos
body = (redness > 0.35).astype(np.uint8) * 255
body = cv2.morphologyEx(body, cv2.MORPH_CLOSE, np.ones((9, 9), np.uint8))
cnts, _ = cv2.findContours(body, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
solid = np.zeros_like(body)
big = [c for c in cnts if cv2.contourArea(c) > 2000]
cv2.drawContours(solid, big, -1, 255, -1)
solid = cv2.erode(solid, np.ones((5, 5), np.uint8))
alpha = np.maximum(redness, solid.astype(np.float32) / 255.0)
alpha = cv2.GaussianBlur(alpha, (3, 3), 0)
rgba = np.dstack([red_clean, (alpha * 255).astype(np.uint8)])
cv2.imwrite(os.path.join(OUT, "btn_selected.png"), rgba)

# --- 2) Iconos (claro sobre oscuro -> alfa por luminancia) ---
icons = {
    "icon_play": (118, 404, 186, 470, red),       # shuriken (sobre rojo)
    "icon_achievements": (112, 500, 172, 562, None),
    "icon_options": (112, 598, 174, 660, None),
    "icon_quit": (112, 696, 176, 760, None),
}
for name, (ix0, iy0, ix1, iy1, _) in icons.items():
    crop = src[iy0:iy1, ix0:ix1].copy()
    L = lum(crop)
    b, g, r = [crop[:, :, i].astype(np.float32) for i in range(3)]
    sat = np.maximum(np.maximum(r, g), b) - np.minimum(np.minimum(r, g), b)
    on_red = name == "icon_play"
    lo, span = (85, 110) if on_red else (55, 70)
    a = np.clip((L - lo) / span, 0, 1) * np.clip(1.0 - (sat - 50) / 60.0, 0, 1)
    # el icono se tiñe en el shader: guardarlo claro y con su textura interior
    gray = np.clip(L * (1.1 if on_red else 1.6), 0, 255).astype(np.uint8)
    out = np.dstack([gray, gray, gray, (a * 255).astype(np.uint8)])
    cv2.imwrite(os.path.join(OUT, name + ".png"), out)

# --- 3) Fondo sin textos en los botones; JUGAR pasa a ser oscuro ---
bg = src.copy()
for (dx0, dy0, dx1, dy1) in DARK:
    region = bg[dy0:dy1, dx0:dx1]
    L = lum(region)
    m = (L > 95).astype(np.uint8) * 255
    # conservar los bordes de pincel (franjas superior/inferior y extremos)
    m[:12, :] = 0
    m[-12:, :] = 0
    m[:, :30] = 0
    m[:, -40:] = 0
    m = cv2.dilate(m, np.ones((5, 5), np.uint8), iterations=2)
    bg[dy0:dy1, dx0:dx1] = cv2.inpaint(region, m, 7, cv2.INPAINT_TELEA)

# Pegar un botón oscuro limpio (el de LOGROS) sobre JUGAR, con borde difuminado.
dx0, dy0, dx1, dy1 = DARK[0]
dark_btn = bg[dy0:dy1, dx0:dx1].copy()
jx0, jy0, jx1, jy1 = JUGAR
# limpiar primero el rojo y su resplandor en la zona de JUGAR
zone = bg[jy0 - 10:jy1 + 10, jx0 - 10:jx1 + 14]
zb, zg, zr = [zone[:, :, i].astype(np.float32) for i in range(3)]
redmask = ((zr - np.maximum(zg, zb)) > 35).astype(np.uint8) * 255
redmask = cv2.dilate(redmask, np.ones((7, 7), np.uint8), iterations=2)
bg[jy0 - 10:jy1 + 10, jx0 - 10:jx1 + 14] = cv2.inpaint(zone, redmask, 9, cv2.INPAINT_TELEA)
dark_resized = cv2.resize(dark_btn, (jx1 - jx0 - 30, jy1 - jy0 - 6), interpolation=cv2.INTER_CUBIC)
py0 = jy0 + 3
px0 = jx0 + 6
h, w = dark_resized.shape[:2]
feather = np.ones((h, w), np.float32)
feather = cv2.GaussianBlur(np.pad(feather[8:-8, 8:-8], 8), (15, 15), 0)[..., None]
target = bg[py0:py0 + h, px0:px0 + w].astype(np.float32)
bg[py0:py0 + h, px0:px0 + w] = (dark_resized * feather + target * (1 - feather)).astype(np.uint8)

# Botón oscuro suelto (para pausa y diálogos): el de LOGROS ya limpio, con bordes difuminados.
dx0, dy0, dx1, dy1 = DARK[0]
dark = bg[dy0:dy1, dx0:dx1].copy()
L = lum(dark)
h, w = L.shape
fa = np.zeros((h, w), np.float32)
fa[6:-6, 10:-10] = 1.0
fa = cv2.GaussianBlur(fa, (0, 0), 4)
a = np.maximum(fa * 0.92, np.clip((L - 60) / 80.0, 0, 1) * fa.clip(0.3, 1))
cv2.imwrite(os.path.join(OUT, "btn_normal.png"), np.dstack([dark, (a * 255).astype(np.uint8)]))

# --- 3b) Capas independientes (logo y lema) y fondo totalmente limpio para escalar a cualquier
#          proporción de pantalla: sin barra de título, sin logo, sin lema y sin botones.
def layer(rect, name, lum_lo, lum_span):
    x0, y0, x1, y1 = rect
    crop = src[y0:y1, x0:x1].copy()
    L = lum(crop)
    b, g, r = [crop[:, :, i].astype(np.float32) for i in range(3)]
    redness = r - np.maximum(g, b)
    # rojo sólo si además es brillante (el humo rojizo oscuro no forma parte del logo)
    a = np.maximum(np.clip((L - lum_lo) / lum_span, 0, 1),
                   np.clip((redness - 28) / 45.0, 0, 1) * np.clip((r - 90) / 60.0, 0, 1))
    a = cv2.GaussianBlur(a, (3, 3), 0)
    # bordes del recorte difuminados para que nunca se vea un rectángulo
    h, w = a.shape
    fy = np.clip(np.minimum(np.arange(h), h - 1 - np.arange(h)) / 24.0, 0, 1)
    fx = np.clip(np.minimum(np.arange(w), w - 1 - np.arange(w)) / 24.0, 0, 1)
    a *= fy[:, None] * fx[None, :]
    cv2.imwrite(os.path.join(OUT, name), np.dstack([crop, (a * 255).astype(np.uint8)]))
    return a

LOGO = (50, 64, 730, 372)
TAGLINE = (60, 860, 480, 1012)
logo_a = layer(LOGO, "logo.png", 62, 70)
tag_a = layer(TAGLINE, "tagline.png", 70, 70)

clean = src.copy()
mask = np.zeros((H, W), np.uint8)
def add_mask(rect, a):
    x0, y0, x1, y1 = rect
    m = (a > 0.12).astype(np.uint8) * 255
    m = cv2.dilate(m, np.ones((9, 9), np.uint8), iterations=2)
    mask[y0:y1, x0:x1] = np.maximum(mask[y0:y1, x0:x1], m)
add_mask(LOGO, logo_a)
add_mask(TAGLINE, tag_a)
mask[TAGLINE[1]:TAGLINE[3], TAGLINE[0]:TAGLINE[0] + 90] = 255  # sello completo
# botones (incluido el resplandor rojo de JUGAR)
for (x0, y0, x1, y1) in [JUGAR] + DARK:
    mask[y0 - 12:y1 + 12, x0 - 12:x1 + 16] = 255
# la zona izquierda se rellena desde una versión reducida (más rápido y sin vetas)
small = cv2.resize(clean, (W // 4, H // 4), interpolation=cv2.INTER_AREA)
small_mask = cv2.resize(mask, (W // 4, H // 4), interpolation=cv2.INTER_NEAREST)
filled_small = cv2.inpaint(small, small_mask, 12, cv2.INPAINT_TELEA)
filled = cv2.resize(filled_small, (W, H), interpolation=cv2.INTER_CUBIC)
# textura de humo/grano para que el relleno no quede plano
rng = np.random.default_rng(7)
noise = cv2.GaussianBlur(rng.normal(0, 1, (H, W)).astype(np.float32), (0, 0), 2.5)
# el relleno hereda el rojo del ensō y del botón JUGAR: se apaga hacia el humo oscuro de alrededor
fb, fg, fr = [filled[:, :, i].astype(np.float32) for i in range(3)]
gray = (fb + fg + fr) / 3.0
filled = np.dstack([gray * 0.9, gray * 0.85, np.minimum(fr, gray * 1.35)]) * 0.6
filled = np.clip(filled * (1.0 + noise[..., None] * 0.35), 0, 255).astype(np.uint8)
# máscara muy difuminada: sin bordes rectos visibles al escalar el fondo
soft = np.clip(cv2.GaussianBlur(mask.astype(np.float32) / 255.0, (0, 0), 28) * 1.6, 0, 1)[..., None]
clean = (filled * soft + clean.astype(np.float32) * (1 - soft)).astype(np.uint8)
# barra de título pintada en la ilustración: se sustituye por la fila inferior repetida
clean[:40] = clean[40:41]
cv2.imwrite(os.path.join(OUT, "background_clean.jpg"), clean, [cv2.IMWRITE_JPEG_QUALITY, 94])

# --- 4) Máscara de fuego: píxeles anaranjados brillantes, excluyendo personajes y la UI ---
b, g, r = [src[:, :, i].astype(np.float32) for i in range(3)]
fire = np.clip((r - b - 70) / 120.0, 0, 1) * np.clip((r - 110) / 100.0, 0, 1)
mask = np.zeros((H, W), np.uint8)
# zona del incendio (todo menos la columna del menú y la barra de título)
mask[40:, 560:] = 255
mask[40:1024, 0:560] = 0
mask[40:360, 0:700] = 0  # logotipo
# personajes: polígonos amplios que se excluyen
man = np.array([[700, 1024], [690, 700], [760, 520], [850, 400], [880, 300], [960, 255], [1060, 280],
                [1100, 380], [1190, 470], [1290, 600], [1300, 800], [1380, 1000], [1400, 1024]], np.int32)
woman = np.array([[1080, 520], [1100, 300], [1120, 170], [1200, 120], [1290, 150], [1350, 260], [1360, 380],
                  [1420, 520], [1470, 700], [1480, 900], [1500, 1024], [1300, 1024], [1250, 700]], np.int32)
cv2.fillPoly(mask, [man, woman], 0)
fire *= mask.astype(np.float32) / 255.0
fire = cv2.GaussianBlur(fire, (0, 0), 3)
fire = np.clip(fire * 1.6, 0, 1)
cv2.imwrite(os.path.join(OUT, "fire_mask.png"), (fire * 255).astype(np.uint8))
print("Recursos generados en", os.path.abspath(OUT))
