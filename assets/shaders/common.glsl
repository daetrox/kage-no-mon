// ---- Utilidades de ruido compartidas por todos los shaders de fragmento ----
float hash11(float n) { return fract(sin(n * 127.1) * 43758.5453123); }
float hash21(vec2 p)
{
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}
vec2 hash22(vec2 p)
{
  float n = hash21(p);
  return vec2(n, hash21(p + n + 17.0));
}
float noise(vec2 p)
{
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  float a = hash21(i);
  float b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0));
  float d = hash21(i + vec2(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm(vec2 p)
{
  float v = 0.0;
  float a = 0.5;
  mat2 rot = mat2(0.8, 0.6, -0.6, 0.8);
  for (int i = 0; i < 5; i++)
  {
    v += a * noise(p);
    p = rot * p * 2.03 + 11.7;
    a *= 0.5;
  }
  return v;
}
float fbm3(vec2 p)
{
  float v = 0.0;
  float a = 0.5;
  mat2 rot = mat2(0.8, 0.6, -0.6, 0.8);
  for (int i = 0; i < 3; i++)
  {
    v += a * noise(p);
    p = rot * p * 2.03 + 11.7;
    a *= 0.5;
  }
  return v;
}
