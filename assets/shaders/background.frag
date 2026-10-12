#version 330 core
// Fondo basado en la ilustración del launcher. El fuego (según fire_mask.png) ondula, parpadea
// y deja calima encima; el resto de la imagen (logotipo, personajes, menú) queda estable.
uniform vec2 uResolution;
uniform float uTime;
uniform vec4 uImgRect;     // x, y, w, h de la imagen en píxeles de pantalla
uniform float uIntensity;  // fundido de entrada 0..1
uniform sampler2D uBg;
uniform sampler2D uMask;
out vec4 fragColor;

vec3 sampleBg(vec2 uv, float lod) { return textureLod(uBg, uv, lod).rgb; }

void main()
{
  // Coordenadas de pantalla con origen arriba-izquierda (como la imagen).
  vec2 px = vec2(gl_FragCoord.x, uResolution.y - gl_FragCoord.y);
  vec2 uv = (px - uImgRect.xy) / uImgRect.zw;
  float T = uTime;

  // Fuera de la imagen (ventanas más anchas que 3:2): reflejo desenfocado y oscuro.
  if (uv.x > 1.0 || uv.y > 1.0 || uv.x < 0.0 || uv.y < 0.0)
  {
    vec2 m = vec2(1.0 - fract(uv.x), clamp(uv.y, 0.0, 1.0));
    vec3 c = sampleBg(m, 5.0) * 0.35;
    fragColor = vec4(c * uIntensity, 1.0);
    return;
  }

  // Máscara de fuego local y ampliada (mip alto = zona alrededor de las llamas).
  float fireNear = texture(uMask, uv).r;
  float fireWide = clamp(textureLod(uMask, uv, 4.0).r * 3.0, 0.0, 1.0);

  // Ondulación: desplazamiento con ruido que sube (las llamas se mueven hacia arriba).
  vec2 q = uv * vec2(1536.0, 1024.0);
  float n1 = noise(vec2(q.x * 0.045, q.y * 0.03 + T * 2.2));
  float n2 = noise(vec2(q.x * 0.09 + 7.0, q.y * 0.07 + T * 3.4));
  vec2 disp = vec2((n1 - 0.5) * 3.5 + (n2 - 0.5) * 1.5, (n2 - 0.5) * 2.0 + 1.2 * fireNear) / vec2(1536.0, 1024.0);
  // Calima: distorsión más suave que alcanza el humo sobre el fuego.
  float hz = noise(vec2(q.x * 0.03, q.y * 0.02 + T * 1.3)) - 0.5;
  vec2 haze = vec2(hz * 1.6, 0.0) / vec2(1536.0, 1024.0);
  vec2 suv = uv + disp * fireNear * 2.2 + haze * fireWide;
  vec3 col = sampleBg(suv, 0.0);

  // Parpadeo y respiración del brillo en las zonas de fuego.
  float flick = 0.85 + 0.3 * noise(vec2(q.x * 0.02 + T * 1.7, q.y * 0.02 - T * 2.9));
  flick *= 0.92 + 0.08 * sin(T * 9.0 + q.y * 0.05);
  col *= mix(1.0, flick, fireNear);
  col += vec3(0.9, 0.3, 0.05) * fireWide * 0.06 * (0.6 + 0.4 * sin(T * 2.3 + q.x * 0.01));

  // Brasas que suben desde el incendio.
  vec3 embers = vec3(0.0);
  for (int layer = 0; layer < 3; layer++)
  {
    float fl = float(layer);
    float scale = 28.0 + fl * 16.0;
    vec2 ep = vec2(uv.x * 1.5, uv.y) * scale;
    ep.y += T * (0.9 + fl * 0.45);
    ep.x += sin(uv.y * 8.0 + T * 0.9 + fl * 2.1) * 0.5 + T * 0.25;
    vec2 cell = floor(ep);
    vec2 f = fract(ep);
    vec2 rnd = hash22(cell + fl * 37.0);
    if (rnd.x > 0.22)
      continue;
    vec2 c = 0.2 + 0.6 * hash22(cell * 1.3 + 9.0);
    float d = length((f - c) * vec2(1.0, 0.55));
    float size = 0.035 + 0.05 * rnd.y;
    float tw = 0.5 + 0.5 * sin(T * (4.0 + rnd.y * 6.0) + rnd.x * 40.0);
    embers += vec3(1.0, 0.4 + 0.3 * rnd.y, 0.08) * smoothstep(size, 0.0, d) * tw * (1.3 - fl * 0.3);
  }
  float region = smoothstep(0.3, 0.55, uv.x) * smoothstep(0.0, 0.25, uv.y);
  col += embers * region;

  // Fundido de la imagen hacia el relleno cuando la ventana es más ancha.
  fragColor = vec4(col * uIntensity, 1.0);
}
