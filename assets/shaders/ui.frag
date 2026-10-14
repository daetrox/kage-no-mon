#version 330 core
// Elementos de interfaz con estética de tinta y pincel japonés.
uniform vec2 uResolution;
uniform float uTime;
uniform int uMode;
uniform vec4 uColor;
uniform vec4 uColor2;
uniform vec4 uRect;   // x, y, w, h en píxeles
uniform vec4 uParam;
uniform sampler2D uTex;
in vec2 vUV;
in vec2 vPix;
out vec4 fragColor;

const float PI = 3.14159265;

float aaw() { return 1.2 / max(uRect.w, 1.0); }

// --- Trazo de pincel horizontal (fondo de botón) ---
vec4 brush(vec2 uv)
{
  float sel = uParam.x;         // 0..1 seleccionado
  float seed = uParam.z * 13.1;
  float aspect = uRect.z / max(uRect.w, 1.0);
  vec2 q = vec2(uv.x * aspect, uv.y);

  // Bordes superior/inferior irregulares.
  float edgeT = 0.14 + 0.06 * fbm(vec2(q.x * 3.0 + seed, 1.0)) + 0.03 * noise(vec2(q.x * 25.0, seed));
  float edgeB = 0.86 - 0.06 * fbm(vec2(q.x * 3.0 + seed, 5.0)) - 0.03 * noise(vec2(q.x * 25.0, seed + 3.0));
  // Extremos deshilachados: rayas horizontales (cerdas del pincel).
  float streak = fbm(vec2(uv.y * 28.0 + seed, 2.0));
  float endL = smoothstep(0.0, 0.05 + 0.08 * streak, uv.x);
  float endR = smoothstep(1.0, 0.86 - 0.12 * streak, uv.x);
  float a = smoothstep(edgeT - 0.02, edgeT + 0.02, uv.y) * smoothstep(edgeB + 0.02, edgeB - 0.02, uv.y);
  a *= endL * endR;
  // Tinta seca: huecos dentro del trazo.
  float dry = fbm(vec2(q.x * 2.0 + seed, uv.y * 22.0));
  a *= smoothstep(0.18, 0.4, dry + 0.15 * sel);

  // Color: tinta oscura con borde gris (normal) o rojo intenso (seleccionado).
  vec3 base = mix(vec3(0.045, 0.04, 0.042), vec3(0.62, 0.03, 0.03), sel);
  base *= 0.75 + 0.5 * fbm(vec2(q.x * 6.0, uv.y * 14.0 + seed));
  // Borde claro que imita pincel gris (sólo en no seleccionado).
  float border = (smoothstep(edgeT + 0.05, edgeT, uv.y) + smoothstep(edgeB - 0.05, edgeB, uv.y)) * (1.0 - sel);
  base = mix(base, vec3(0.42, 0.4, 0.4), clamp(border, 0.0, 1.0) * 0.55);
  // Brillo animado al estar seleccionado.
  float sweep = smoothstep(0.15, 0.0, abs(uv.x - fract(uTime * 0.35) * 1.6 + 0.3)) * sel * 0.25;
  base += vec3(1.0, 0.4, 0.2) * sweep;
  return vec4(base, a * uColor.a);
}

// --- Ensō: círculo rojo de pincel detrás del logotipo ---
vec4 enso(vec2 uv)
{
  vec2 p = uv * 2.0 - 1.0;
  float r = length(p);
  float ang = atan(p.y, p.x);
  float t = fract((ang + PI) / (2.0 * PI) + 0.12);
  // Grosor variable: grueso al comenzar, se afina y se abre al final.
  float thick = mix(0.13, 0.025, smoothstep(0.0, 0.92, t)) * (0.85 + 0.3 * noise(vec2(ang * 3.0, 1.0)));
  float rad = 0.78 + 0.03 * sin(ang * 2.0 + 1.0);
  float ring = smoothstep(thick, thick - 0.02, abs(r - rad));
  ring *= smoothstep(0.98, 0.9, t);
  float dry = fbm(vec2(ang * 9.0, r * 30.0));
  ring *= smoothstep(0.22, 0.45, dry + (1.0 - t) * 0.25);
  vec3 c = uColor.rgb * (0.75 + 0.4 * fbm(p * 6.0));
  return vec4(c, ring * uColor.a);
}

// --- Iconos vectoriales ---
float sdBox(vec2 p, vec2 b)
{
  vec2 d = abs(p) - b;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}
float sdSegment(vec2 p, vec2 a, vec2 b)
{
  vec2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h);
}

float shuriken(vec2 p)
{
  float ang = uTime * 0.6 * uParam.y; // gira si uParam.y > 0
  p = mat2(cos(ang), -sin(ang), sin(ang), cos(ang)) * p;
  float a = atan(p.y, p.x);
  float r = length(p);
  float star = 0.62 * pow(abs(cos(a * 2.0)), 6.0) + 0.22;
  float d = r - star;
  d = max(d, -(r - 0.09)); // agujero central
  return d;
}

float gear(vec2 p)
{
  float a = atan(p.y, p.x);
  float r = length(p);
  float teeth = smoothstep(-0.25, 0.25, cos(a * 8.0)) * 0.13;
  float d = r - (0.58 + teeth);
  d = max(d, -(r - 0.22));
  return d;
}

float door(vec2 p)
{
  float frame = sdBox(p - vec2(-0.12, 0.0), vec2(0.36, 0.62));
  float inner = sdBox(p - vec2(-0.12, 0.0), vec2(0.26, 0.52));
  float d = max(frame, -inner);
  float leaf = sdBox(p - vec2(-0.2, 0.0), vec2(0.18, 0.5));
  d = min(d, leaf);
  float arrow = sdSegment(p, vec2(0.05, 0.0), vec2(0.62, 0.0)) - 0.07;
  float head1 = sdSegment(p, vec2(0.62, 0.0), vec2(0.4, 0.22)) - 0.07;
  float head2 = sdSegment(p, vec2(0.62, 0.0), vec2(0.4, -0.22)) - 0.07;
  return min(d, min(arrow, min(head1, head2)));
}

float scroll(vec2 p)
{
  float body = sdBox(p, vec2(0.42, 0.5));
  float rodT = sdBox(p - vec2(0.0, 0.55), vec2(0.6, 0.08)) - 0.02;
  float rodB = sdBox(p - vec2(0.0, -0.55), vec2(0.6, 0.08)) - 0.02;
  float lines = 1e9;
  for (int i = 0; i < 3; i++)
  {
    float y = 0.25 - float(i) * 0.25;
    lines = min(lines, sdSegment(p, vec2(-0.25, y), vec2(0.25, y)) - 0.04);
  }
  return min(max(body, -lines), min(rodT, rodB));
}


// --- Iconos de botones (uParam.x = tipo) ---
//  0 tecla (teclado)  1 círculo relleno (Xbox, el texto va encima)
//  2 cruz PS  3 círculo PS  4 cuadrado PS  5 triángulo PS  6 gatillo/bumper (píldora)
vec4 padGlyph(vec2 uv)
{
  int kind = int(uParam.x + 0.5);
  float aspect = uRect.z / max(uRect.w, 1.0);
  vec2 p = (uv * 2.0 - 1.0) * vec2(aspect, -1.0);
  float px = 2.0 / uRect.w;
  if (kind == 0 || kind == 6)
  {
    // tecla / píldora: rectángulo redondeado con borde claro y relleno oscuro
    float r = kind == 6 ? 0.9 : 0.35;
    vec2 b = vec2(aspect - 0.06, 0.94);
    vec2 q = abs(p) - b + r;
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    float fill = smoothstep(px, -px, d);
    float border = smoothstep(px, -px, abs(d + 0.07) - 0.07);
    vec3 c = mix(vec3(0.08, 0.075, 0.075), uColor.rgb, border);
    return vec4(c, fill * uColor.a);
  }
  float circle = length(p) - 0.94;
  float fillA = smoothstep(px, -px, circle);
  if (kind == 1)
    return vec4(uColor.rgb, fillA * uColor.a);
  // PlayStation: disco oscuro con el símbolo de color
  float d = 1.0;
  if (kind == 2) d = min(sdSegment(p, vec2(-0.42, -0.42), vec2(0.42, 0.42)), sdSegment(p, vec2(-0.42, 0.42), vec2(0.42, -0.42))) - 0.09;
  else if (kind == 3) d = abs(length(p) - 0.45) - 0.09;
  else if (kind == 4) d = abs(sdBox(p, vec2(0.4))) - 0.09;
  else if (kind == 5)
  {
    vec2 a = vec2(0.0, 0.48), b2 = vec2(-0.46, -0.34), c2 = vec2(0.46, -0.34);
    d = min(min(sdSegment(p, a, b2), sdSegment(p, b2, c2)), sdSegment(p, c2, a)) - 0.09;
  }
  float sym = smoothstep(px, -px, d);
  vec3 c = mix(vec3(0.1, 0.1, 0.11), uColor.rgb, sym);
  return vec4(c, fillA * uColor.a);
}

vec4 icon(vec2 uv)
{
  vec2 p = (uv * 2.0 - 1.0) * vec2(1.0, -1.0);
  int id = int(uParam.x + 0.5);
  float px = 2.0 / uRect.w;
  float d = 1.0;
  if (id == 0) d = shuriken(p);
  else if (id == 1) d = gear(p);
  else if (id == 2) d = door(p);
  else if (id == 3) d = scroll(p);
  else if (id == 4) d = min(sdSegment(p, vec2(-0.2, 0.45), vec2(0.25, 0.0)), sdSegment(p, vec2(-0.2, -0.45), vec2(0.25, 0.0))) - 0.09;
  else if (id == 5) d = min(sdSegment(p, vec2(-0.5, -0.5), vec2(0.5, 0.5)), sdSegment(p, vec2(-0.5, 0.5), vec2(0.5, -0.5))) - 0.05;
  else if (id == 6) d = sdSegment(p, vec2(-0.5, 0.0), vec2(0.5, 0.0)) - 0.05;
  else if (id == 7) d = abs(sdBox(p, vec2(0.48))) - 0.05;
  else if (id == 8)
  {
    // Sello hanko: cuadrado rojo de bordes gastados.
    float n = fbm(uv * 9.0) * 0.12;
    float box = abs(sdBox(p, vec2(0.86))) - 0.06 - n * 0.5;
    float a = smoothstep(px, -px, box) * smoothstep(0.25, 0.45, fbm(uv * 20.0) + 0.2);
    return vec4(uColor.rgb, a * uColor.a);
  }
  else if (id == 9)
  {
    // Subrayado de pincel fino que se afina hacia la derecha.
    float w = mix(0.5, 0.05, uv.x) * (0.8 + 0.4 * noise(vec2(uv.x * 30.0, 1.0)));
    float a = smoothstep(w, w * 0.6, abs(uv.y - 0.5) * 2.0) * smoothstep(0.0, 0.03, uv.x);
    a *= smoothstep(0.2, 0.4, fbm(vec2(uv.x * 40.0, uv.y * 3.0)) + 0.1);
    return vec4(uColor.rgb, a * uColor.a);
  }
  else if (id == 10) d = min(sdSegment(p, vec2(0.2, 0.45), vec2(-0.25, 0.0)), sdSegment(p, vec2(0.2, -0.45), vec2(-0.25, 0.0))) - 0.09;
  else if (id == 11) d = min(sdSegment(p, vec2(-0.2, 0.45), vec2(0.25, 0.0)), sdSegment(p, vec2(-0.2, -0.45), vec2(0.25, 0.0))) - 0.09;
  else if (id == 12)
  {
    // Marca de enemigo fijado: punta de flecha hacia abajo, con borde oscuro.
    vec2 a = vec2(-0.62, 0.55), b = vec2(0.62, 0.55), c = vec2(0.0, -0.62);
    float e1 = dot(p - a, normalize(vec2(b.y - a.y, a.x - b.x)));
    float e2 = dot(p - b, normalize(vec2(c.y - b.y, b.x - c.x)));
    float e3 = dot(p - c, normalize(vec2(a.y - c.y, c.x - a.x)));
    float tri = max(max(-e1, -e2), -e3) + 0.06; // vértices en sentido horario: normales invertidas
    float notch = length(p - vec2(0.0, 0.62)) - 0.42; // muesca superior: forma de punta de kunai
    float dm = max(tri, -notch);
    float fill = smoothstep(px, -px, dm);
    float rim = smoothstep(px, -px, dm - 0.12);
    return vec4(mix(vec3(0.05, 0.0, 0.0), uColor.rgb, fill), rim * uColor.a);
  }
  float a = smoothstep(px, -px, d);
  return vec4(uColor.rgb, a * uColor.a);
}

// --- Panel translúcido con bordes rasgados ---
vec4 panel(vec2 uv)
{
  vec2 q = uv * vec2(uRect.z, uRect.w) / 300.0;
  float edge = 0.02 + 0.015 * fbm(q * 6.0);
  float ex = min(uv.x, 1.0 - uv.x) * uRect.z / uRect.w;
  float ey = min(uv.y, 1.0 - uv.y);
  float a = smoothstep(edge * 0.4, edge * 2.0, min(ex, ey));
  vec3 c = uColor.rgb * (0.85 + 0.3 * fbm(q * 3.0));
  // Línea roja fina en el borde izquierdo.
  float line = smoothstep(0.004, 0.0, abs(uv.x - 0.012)) * step(0.05, uv.y) * step(uv.y, 0.95);
  c = mix(c, uColor2.rgb, line * uColor2.a);
  return vec4(c, a * uColor.a);
}

// --- Barrido de tinta a pantalla completa (apertura y cierre) ---
// uParam.x = progreso 0..1: a la derecha del borde de pincel queda negro.
vec4 inkWipe(vec2 uv)
{
  float p = uParam.x;
  float edge = mix(-0.3, 1.3, p);
  edge += (fbm(vec2(uv.y * 4.0, 3.0)) - 0.5) * 0.28;          // borde irregular de pincel
  edge += (noise(vec2(uv.y * 60.0, 1.0)) - 0.5) * 0.035;      // cerdas
  float d = uv.x - edge;
  float a = smoothstep(-0.012, 0.012, d);
  // salpicaduras de tinta por delante del borde
  float splat = step(0.82, noise(uv * vec2(40.0, 22.0))) * smoothstep(0.08, 0.0, abs(d + 0.03));
  a = max(a, splat);
  // filo rojo, como tinta sobre brasas
  float rim = smoothstep(0.05, 0.0, abs(d - 0.02)) * (0.5 + 0.5 * fbm(uv * 20.0));
  vec3 c = mix(vec3(0.0), vec3(0.55, 0.04, 0.02), rim);
  return vec4(c, max(a, rim * 0.9) * uColor.a);
}

void main()
{
  vec2 uv = vUV;
  if (uMode == 0)
  {
    fragColor = uColor;
  }
  else if (uMode == 1)
  {
    float a = texture(uTex, uv).r;
    vec3 c = uColor.rgb;
    if (uParam.x > 0.0)
    {
      // Logotipo: degradado vertical + desgaste de tinta.
      float ty = clamp((vPix.y - uRect.y) / max(uRect.w, 1.0), 0.0, 1.2);
      c = mix(uColor.rgb, uColor2.rgb, smoothstep(0.1, 0.95, ty));
      vec2 sp = vPix / uRect.w;
      float wear = fbm(sp * vec2(9.0, 14.0));
      float scratches = smoothstep(0.7, 0.76, noise(vec2(sp.x * 3.0 + sp.y * 5.0, sp.y * 40.0 + sp.x * 6.0)));
      a *= smoothstep(0.14, 0.3, wear) * (1.0 - scratches * 0.7);
      c *= 0.8 + 0.35 * fbm(sp * 30.0);
    }
    fragColor = vec4(c, a * uColor.a);
  }
  else if (uMode == 2)
  {
    fragColor = vec4(texture(uTex, uv).rgb, 1.0) * uColor;
  }
  else if (uMode == 3) fragColor = brush(uv);
  else if (uMode == 4) fragColor = enso(uv);
  else if (uMode == 5) fragColor = icon(uv);
  else if (uMode == 6) fragColor = panel(uv);
  else if (uMode == 7) fragColor = texture(uTex, uv) * uColor;
  else if (uMode == 8) fragColor = padGlyph(uv);
  else if (uMode == 10) fragColor = inkWipe(uv);
  else if (uMode == 9)
  {
    vec4 t = texture(uTex, uv);
    float g = dot(t.rgb, vec3(0.3, 0.59, 0.11));
    fragColor = vec4(vec3(g) * 0.55, t.a) * uColor;
  }
  else fragColor = uColor;
}
