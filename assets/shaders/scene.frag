#version 330 core
// Escena de fondo 100% procedural: castillo japonés (tenshu) en llamas bajo una luna roja.
// Todo se genera con matemáticas; no se usa ninguna imagen con derechos de autor.
uniform vec2 uResolution;
uniform float uTime;
uniform vec2 uMouse;      // 0..1, para un leve paralaje
uniform float uIntensity; // 0..1, fundido de entrada
out vec4 fragColor;

float R;          // relación de aspecto (x va de 0 a R, y de 0 a 1)
float T;          // tiempo

// ------------------------------------------------------------------------------------------------
// Fuego
// ------------------------------------------------------------------------------------------------

// Llama individual en coordenadas locales: q.x en [-1,1] (ancho), q.y en [0,~2] (alto).
// Las lenguas de fuego ondulan: el desplazamiento lateral crece con la altura.
float flame(vec2 q, float seed)
{
  if (q.y < -0.3 || q.y > 2.6 || abs(q.x) > 2.0)
    return 0.0;
  float h = clamp(q.y, 0.0, 2.5);
  // Ondulación: ondas senoidales que suben + turbulencia.
  q.x += sin(q.y * 3.2 - T * 2.6 + seed * 6.28) * 0.16 * h;
  q.x += sin(q.y * 7.0 - T * 4.1 + seed * 3.1) * 0.05 * h;
  q.x += (fbm3(vec2(q.y * 1.3 - T * 1.1, seed * 13.0)) - 0.5) * 0.7 * h;

  float n = fbm(vec2(q.x * 3.0 + seed * 7.0, q.y * 2.6 - T * 3.0));
  float n2 = noise(vec2(q.x * 7.0 - seed, q.y * 6.0 - T * 6.0));

  // Ancho modulado por ruido: varias lenguas de fuego en vez de una gota.
  float width = max(1.0 - 0.42 * h, 0.04) * (0.75 + 0.5 * noise(vec2(q.y * 2.0 - T * 1.5, seed * 5.0)));
  float body = 1.0 - smoothstep(0.1, 1.0, abs(q.x) / width);
  float vert = smoothstep(-0.25, 0.1, q.y) * (1.0 - smoothstep(0.4, 2.3, q.y));
  float f = body * vert;
  f = f * 1.55 - n * (0.5 + q.y * 0.75) - n2 * 0.22;
  // Las puntas se enfrían (más rojas) a medida que suben.
  f *= 1.0 - 0.3 * clamp(q.y, 0.0, 1.5);
  return clamp(f, 0.0, 1.0);
}

vec3 fireColor(float f)
{
  vec3 c = mix(vec3(0.3, 0.01, 0.0), vec3(0.85, 0.1, 0.02), smoothstep(0.0, 0.35, f));
  c = mix(c, vec3(1.0, 0.38, 0.05), smoothstep(0.35, 0.7, f));
  c = mix(c, vec3(1.0, 0.8, 0.35), smoothstep(0.75, 1.0, f));
  return c * 1.25;
}

// Mezcla una capa de fuego sobre el color: alfa suave en los bordes, aditivo en el núcleo.
vec3 addFire(vec3 col, float f, float strength)
{
  float a = smoothstep(0.0, 0.45, f) * strength;
  return mix(col, fireColor(f), a * 0.8) + fireColor(f) * f * f * 0.3 * strength;
}

// ------------------------------------------------------------------------------------------------
// Castillo
// ------------------------------------------------------------------------------------------------

float CX;           // centro del castillo
const float BASE_Y = 0.115;
const int TIERS = 5;

// Devuelve cobertura del castillo (0/1 suavizado). `win` = brillo de ventanas encendidas.
float castle(vec2 p, out float win, out float rimY)
{
  win = 0.0;
  rimY = 0.0;
  float aa = 1.5 / uResolution.y;
  float x = p.x - CX;
  float ax = abs(x);
  float m = 0.0;

  // Basamento de piedra con muros inclinados.
  float pb0 = BASE_Y, pb1 = BASE_Y + 0.11;
  if (p.y < pb1)
  {
    float s = clamp((p.y - pb0) / (pb1 - pb0), 0.0, 1.0);
    float hw = mix(0.29, 0.225, pow(s, 0.7));
    m = max(m, smoothstep(aa, -aa, ax - hw) * step(p.y, pb1));
  }

  float yb = pb1;
  float ww = 0.19;   // medio ancho del muro
  float wh = 0.07;   // alto del muro
  float rh = 0.05;   // alto del tejado
  for (int i = 0; i < TIERS; i++)
  {
    bool top = (i == TIERS - 1);
    // Muro del piso.
    float w0 = yb, w1 = yb + wh;
    if (p.y >= w0 - aa && p.y <= w1 + aa)
    {
      float inside = smoothstep(aa, -aa, ax - ww);
      m = max(m, inside);
      // Ventanas: rejilla regular, algunas ardiendo por dentro.
      float cells = 7.0 - float(i);
      float cx = (x / ww * 0.5 + 0.5) * cells;
      float id = floor(cx);
      float fx = fract(cx);
      float fy = (p.y - w0) / wh;
      float wmask = step(0.4, fx) * step(fx, 0.6) * step(0.32, fy) * step(fy, 0.62);
      float lit = step(0.45, hash21(vec2(id, float(i) * 7.0)));
      float flick = 0.65 + 0.35 * noise(vec2(T * 6.0 + id * 3.0, float(i)));
      win = max(win, inside * wmask * lit * flick);
    }
    // Tejado con aleros curvados hacia arriba en las esquinas.
    float rw = ww * (top ? 1.5 : 1.42);
    float tw = top ? 0.012 : ww * 0.78;
    float r0 = w1 - 0.006;
    float r1 = w1 + (top ? rh * 1.6 : rh);
    float t = ax / rw;
    float eave = r0 + 0.028 * pow(t, 5.0) - 0.006;
    if (p.y > eave - aa && p.y < r1 + 0.04)
    {
      float s = clamp((p.y - r0) / (r1 - r0), 0.0, 1.0);
      float hw = mix(rw, tw, pow(s, 0.55));
      // Ligera curva cóncava del faldón.
      hw -= 0.012 * sin(s * 3.14159);
      float roof = smoothstep(aa, -aa, ax - hw) * smoothstep(-aa, aa, p.y - eave) * step(p.y, r1);
      m = max(m, roof);
      // Línea de luz del fuego en el canto del tejado.
      rimY = max(rimY, roof * smoothstep(0.007, 0.0, p.y - eave));
      if (top)
      {
        // Shachihoko: ornamentos en los extremos de la cumbrera.
        vec2 o = vec2(ax - 0.034 + (p.y - r1) * 0.5, p.y - r1 - 0.006);
        float orn = smoothstep(aa, -aa, length(o * vec2(1.6, 0.7)) - 0.006);
        m = max(m, orn);
        m = max(m, smoothstep(aa, -aa, ax - 0.036) * step(r1 - 0.004, p.y) * step(p.y, r1 + 0.006));
      }
    }
    yb = r1 - 0.004;
    ww *= 0.8;
    wh *= 0.9;
  }
  return m;
}

// Fuentes de fuego del castillo: (x relativo al centro, y base, ancho, alto)
const int NFIRES = 14;
vec4 fireSrc(int i)
{
  if (i == 0) return vec4(-0.21, 0.215, 0.10, 0.10);
  if (i == 1) return vec4(0.17, 0.222, 0.09, 0.09);
  if (i == 2) return vec4(-0.05, 0.225, 0.07, 0.07);
  if (i == 3) return vec4(-0.11, 0.315, 0.09, 0.12);
  if (i == 4) return vec4(0.12, 0.32, 0.07, 0.10);
  if (i == 5) return vec4(0.02, 0.33, 0.05, 0.06);
  if (i == 6) return vec4(-0.08, 0.405, 0.06, 0.09);
  if (i == 7) return vec4(0.10, 0.41, 0.05, 0.08);
  if (i == 8) return vec4(0.03, 0.49, 0.07, 0.13);
  if (i == 9) return vec4(-0.05, 0.565, 0.05, 0.10);
  if (i == 10) return vec4(0.04, 0.63, 0.04, 0.09);
  if (i == 11) return vec4(0.28, 0.12, 0.07, 0.08);
  if (i == 12) return vec4(-0.30, 0.115, 0.08, 0.10);
  return vec4(-0.18, 0.125, 0.05, 0.06);
}

// ------------------------------------------------------------------------------------------------
// Pueblo en llamas (segundo plano)
// ------------------------------------------------------------------------------------------------

float townHeight(float id) { return 0.17 + 0.07 * hash11(id * 3.7); }

// Silueta de casas con tejados a dos aguas.
float town(vec2 p, out float wins)
{
  wins = 0.0;
  float cw = 0.075;
  float cx = p.x / cw;
  float id = floor(cx);
  float fx = fract(cx) - 0.5;
  float h = townHeight(id);
  float roofH = 0.035;
  float wall = step(abs(fx), 0.42) * step(p.y, h - roofH);
  float roof = step(p.y, h - abs(fx) * roofH * 2.2) * step(abs(fx), 0.55);
  float lit = step(0.6, hash11(id * 9.1)) * step(abs(fx), 0.15) * step(h - roofH - 0.03, p.y) *
              step(p.y, h - roofH - 0.012);
  wins = lit * (0.6 + 0.4 * noise(vec2(T * 5.0, id)));
  return max(wall, roof);
}

// ------------------------------------------------------------------------------------------------
// Torii (primer plano, derecha)
// ------------------------------------------------------------------------------------------------

float torii(vec2 p, vec2 c, float s)
{
  vec2 q = (p - c) / s;
  float aa = 2.0 / (uResolution.y * s);
  float ax = abs(q.x);
  // Pilares ligeramente inclinados.
  float pillar = smoothstep(aa, -aa, abs(ax - 0.36 + q.y * 0.03) - 0.045) * step(q.y, 0.92) * step(-1.0, q.y);
  // Kasagi (dintel superior curvado) y shimaki.
  float topY = 0.95 + 0.05 * pow(ax / 0.62, 3.0);
  float kasagi = smoothstep(aa, -aa, abs(q.y - topY) - 0.045) * step(ax, 0.62 + 0.02 * q.y);
  float nuki = smoothstep(aa, -aa, abs(q.y - 0.72) - 0.028) * step(ax, 0.5);
  float gakuzuka = smoothstep(aa, -aa, ax - 0.03) * step(0.72, q.y) * step(q.y, 0.95);
  return max(max(pillar, kasagi), max(nuki, gakuzuka));
}


// ------------------------------------------------------------------------------------------------
// Ninja agazapado sobre el torii, con la bufanda ondeando al viento
// ------------------------------------------------------------------------------------------------

float sdCapsule(vec2 p, vec2 a, vec2 b, float r)
{
  vec2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h) - r;
}

float ninja(vec2 p, vec2 c, float s)
{
  vec2 q = (p - c) / s;
  float aa = 2.0 / (uResolution.y * s);
  float d = length(q - vec2(0.02, 0.78)) - 0.13;                         // cabeza
  d = min(d, sdCapsule(q, vec2(0.0, 0.62), vec2(-0.12, 0.3), 0.16));     // torso inclinado
  d = min(d, sdCapsule(q, vec2(-0.12, 0.3), vec2(0.18, 0.22), 0.1));    // muslo
  d = min(d, sdCapsule(q, vec2(0.18, 0.22), vec2(0.12, 0.0), 0.08));    // pierna
  d = min(d, sdCapsule(q, vec2(-0.1, 0.28), vec2(-0.3, 0.12), 0.09));   // muslo trasero
  d = min(d, sdCapsule(q, vec2(-0.3, 0.12), vec2(-0.2, 0.0), 0.07));    // rodilla atrás
  d = min(d, sdCapsule(q, vec2(0.03, 0.56), vec2(0.25, 0.4), 0.06));    // brazo
  d = min(d, sdCapsule(q, vec2(0.25, 0.4), vec2(0.28, 0.12), 0.055));   // antebrazo apoyado
  d = min(d, sdCapsule(q, vec2(-0.32, 0.25), vec2(0.28, 0.95), 0.025)); // espada a la espalda
  // Bufanda: cinta que ondea con el viento.
  float sx = clamp((-q.x - 0.05) / 0.85, 0.0, 1.0);
  float wave = sin(sx * 7.0 - T * 6.0) * 0.06 * sx + sin(sx * 13.0 - T * 9.0) * 0.02 * sx;
  float scarfY = 0.66 + sx * 0.1 + wave;
  float scarf = abs(q.y - scarfY) - mix(0.05, 0.015, sx);
  scarf = max(scarf, q.x + 0.02);
  scarf = max(scarf, -q.x - 0.9);
  d = min(d, scarf);
  return smoothstep(aa, -aa, d);
}

// ------------------------------------------------------------------------------------------------

void main()
{
  R = uResolution.x / uResolution.y;
  T = uTime;
  vec2 p = gl_FragCoord.xy / uResolution.y;
  vec2 par = (uMouse - 0.5) * vec2(1.0, -1.0);
  // Composición anclada al borde derecho (el menú vive a la izquierda).
  CX = R - 0.6;

  // ---- Distorsión por calor sobre las zonas en llamas ----
  float heat = exp(-pow((p.x - CX) / 0.32, 2.0)) * smoothstep(0.1, 0.35, p.y) * (1.0 - smoothstep(0.6, 1.0, p.y));
  vec2 haze = vec2(noise(vec2(p.x * 40.0, p.y * 25.0 - T * 4.0)) - 0.5,
                   noise(vec2(p.x * 35.0 + 7.0, p.y * 30.0 - T * 3.5)) - 0.5) * 0.0045 * heat;

  // ---- Cielo ----
  vec2 ps = p + par * 0.004;
  vec3 col = mix(vec3(0.20, 0.025, 0.02), vec3(0.015, 0.004, 0.006), smoothstep(0.15, 0.95, ps.y));
  // Resplandor del incendio en el horizonte.
  float glowSky = exp(-pow((ps.x - CX) / 0.55, 2.0)) * exp(-pow((ps.y - 0.35) / 0.35, 2.0));
  col += vec3(0.75, 0.16, 0.03) * glowSky;

  // Nubes de tormenta.
  float clouds = fbm(vec2(ps.x * 1.6 + T * 0.012, ps.y * 3.0));
  col = mix(col, col * 0.45 + vec3(0.06, 0.008, 0.006), smoothstep(0.45, 0.75, clouds) * 0.7);

  // ---- Luna de sangre ----
  vec2 mc = vec2(R - 0.27, 0.77) + par * 0.006;
  float md = length(ps - mc);
  float mr = 0.125;
  float moon = smoothstep(mr + 0.002, mr - 0.002, md);
  vec3 moonCol = vec3(0.62, 0.09, 0.05) * (0.75 + 0.45 * fbm((ps - mc) * 14.0));
  moonCol *= 1.0 - 0.35 * smoothstep(0.4 * mr, mr, length(ps - mc - vec2(0.03, -0.02)));
  col += vec3(0.35, 0.04, 0.02) * exp(-max(md - mr, 0.0) * 14.0) * 0.6;
  col = mix(col, moonCol, moon);
  float mcloud = smoothstep(0.5, 0.8, fbm(vec2(ps.x * 4.0 - T * 0.02, ps.y * 9.0))) * smoothstep(0.62, 0.8, ps.y) * smoothstep(0.92, 0.8, ps.y);
  col = mix(col, vec3(0.05, 0.01, 0.01), mcloud * 0.75);

  // ---- Montañas lejanas ----
  float mtn = 0.36 + 0.07 * fbm(vec2(ps.x * 2.5, 1.0)) - 0.05 * abs(sin(ps.x * 2.0));
  float mtnMask = smoothstep(0.002, -0.002, ps.y - mtn);
  col = mix(col, vec3(0.06, 0.012, 0.012) + vec3(0.12, 0.02, 0.0) * glowSky, mtnMask * 0.85);

  // ---- Humo que sale del castillo (sube y deriva) ----
  vec2 sp = p + par * 0.008;
  float plumeX = CX + (sp.y - 0.55) * 0.35 + sin(sp.y * 3.0 + T * 0.2) * 0.03;
  float plumeW = 0.08 + max(sp.y - 0.5, 0.0) * 0.5;
  float plume = exp(-pow((sp.x - plumeX) / plumeW, 2.0)) * smoothstep(0.45, 0.7, sp.y);
  float smokeN = fbm(vec2(sp.x * 3.0 - T * 0.04, sp.y * 2.5 - T * 0.12));
  float smoke = clamp(plume * smoothstep(0.25, 0.75, smokeN) * 1.8, 0.0, 0.95);
  vec3 smokeCol = mix(vec3(0.03, 0.02, 0.02), vec3(0.32, 0.07, 0.02), smoothstep(0.9, 0.5, sp.y) * 0.7);
  col = mix(col, smokeCol, smoke);

  // ---- Pueblo en llamas ----
  vec2 pt = p + par * 0.012 + haze * 0.6;
  float twins;
  float townM = town(vec2(pt.x, pt.y), twins);
  vec3 townCol = vec3(0.025, 0.006, 0.006) + vec3(0.18, 0.04, 0.0) * glowSky;
  col = mix(col, townCol, townM);
  col += vec3(1.0, 0.45, 0.1) * twins * 0.9 * smoothstep(R * 0.35, R * 0.6, pt.x);
  // Llamas sobre algunas casas.
  float townFire = 0.0;
  {
    float cw = 0.075;
    float id0 = floor(pt.x / cw);
    for (int k = -1; k <= 1; k++)
    {
      float id = id0 + float(k);
      if (hash11(id * 5.3 + 1.0) < 0.55 || (id + 0.5) * cw < R * 0.5)
        continue;
      float h = townHeight(id);
      vec2 q = vec2((pt.x - (id + 0.5) * cw) / (cw * 0.5), (pt.y - h + 0.03) / 0.09);
      townFire = max(townFire, flame(q, id * 0.37));
    }
  }
  col = addFire(col, townFire, 0.8);

  // ---- Castillo ----
  vec2 pc = p + par * 0.018 + haze;
  float win, rim;
  float cm = castle(pc, win, rim);
  float castleLight = exp(-pow((pc.y - 0.3) / 0.35, 2.0));
  vec3 castleCol = vec3(0.008, 0.003, 0.004) + vec3(0.07, 0.012, 0.0) * castleLight;
  // Textura sutil de tablones/tejas.
  castleCol *= 0.8 + 0.4 * noise(vec2(pc.x * 260.0, pc.y * 60.0));
  col = mix(col, castleCol, cm);
  col += vec3(1.0, 0.36, 0.05) * rim * cm * (0.5 + 0.6 * noise(vec2(pc.x * 30.0, T * 2.0)));
  col += vec3(1.0, 0.45, 0.1) * win * 1.1;

  // ---- Fuego del castillo ----
  float fireSum = 0.0;
  float halo = 0.0;
  for (int i = 0; i < NFIRES; i++)
  {
    vec4 f = fireSrc(i);
    vec2 q = vec2((pc.x - (CX + f.x)) / f.z, (pc.y - f.y) / f.w);
    fireSum = max(fireSum, flame(q, float(i) * 0.731 + 0.2));
    vec2 hq = vec2(q.x * 0.45, (q.y - 0.5) * 0.6);
    float flick = 0.8 + 0.2 * sin(T * 7.0 + float(i) * 2.3) * sin(T * 3.1 + float(i));
    halo += exp(-dot(hq, hq) * 2.2) * f.z * 6.0 * flick;
  }
  col += vec3(0.9, 0.22, 0.03) * halo * 0.22;
  // Brillo difuso alrededor de las llamas.
  col += vec3(0.6, 0.12, 0.02) * heat * 0.25;
  col = addFire(col, fireSum, 1.0);

  // ---- Torii en primer plano ----
  vec2 pf = p + par * 0.03;
  float tor = torii(pf, vec2(R - 0.13, 0.0), 0.38);
  vec3 torCol = vec3(0.012, 0.003, 0.004) + vec3(0.12, 0.015, 0.0) * smoothstep(0.0, 0.4, pf.y);
  col = mix(col, torCol, tor);
  float nin = ninja(pf, vec2(R - 0.13 - 0.16, 0.38 * 1.02), 0.085);
  col = mix(col, vec3(0.006, 0.002, 0.003), nin);

  // ---- Suelo / primer plano oscuro ----
  float ground = smoothstep(0.13, 0.0, pf.y + 0.02 * fbm(vec2(pf.x * 5.0, 0.0)));
  col = mix(col, vec3(0.01, 0.002, 0.003), ground * 0.9);

  // ---- Brasas flotando ----
  vec3 embers = vec3(0.0);
  for (int layer = 0; layer < 3; layer++)
  {
    float fl = float(layer);
    float scale = 14.0 + fl * 10.0;
    float speed = 0.06 + fl * 0.035;
    vec2 ep = p * scale;
    ep.y -= T * speed * scale;
    ep.x += sin(p.y * 6.0 + T * 0.8 + fl * 2.0) * 0.6 + T * 0.4;
    vec2 cell = floor(ep);
    vec2 fpos = fract(ep);
    vec2 rnd = hash22(cell + fl * 31.0);
    if (rnd.x > 0.32)
      continue;
    vec2 center = 0.2 + 0.6 * hash22(cell * 1.7 + 3.0);
    float d = length((fpos - center) * vec2(1.0, 0.6));
    float size = 0.04 + 0.05 * rnd.y;
    float tw = 0.5 + 0.5 * sin(T * (3.0 + rnd.y * 5.0) + rnd.x * 30.0);
    float bright = smoothstep(size, 0.0, d) * tw;
    float region = smoothstep(0.25, 0.9, p.x / R + 0.2) * (0.4 + 0.6 * smoothstep(1.0, 0.2, p.y));
    embers += vec3(1.0, 0.35 + 0.3 * rnd.y, 0.06) * bright * region * (1.4 - fl * 0.3);
  }
  col += embers;

  // ---- Oscurecer el lado del menú para que el texto se lea ----
  float side = smoothstep(0.05, 0.95, p.x / max(R * 0.62, 0.1));
  col *= mix(0.22, 1.0, side);

  // ---- Viñeta, contraste y grano ----
  vec2 uv = gl_FragCoord.xy / uResolution;
  float vig = smoothstep(1.25, 0.35, length((uv - vec2(0.55, 0.5)) * vec2(1.0, 1.15)));
  col *= mix(0.45, 1.0, vig);
  col = pow(max(col, 0.0), vec3(1.08));
  col = col / (1.0 + col * 0.35); // tonemapping suave
  col += (hash21(gl_FragCoord.xy + fract(T) * 100.0) - 0.5) * 0.025;
  col *= uIntensity;

  fragColor = vec4(col, 1.0);
}
