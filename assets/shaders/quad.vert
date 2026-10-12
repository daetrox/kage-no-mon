#version 330 core
layout(location = 0) in vec2 aPos; // píxeles, origen arriba-izquierda
layout(location = 1) in vec2 aUV;
uniform vec2 uResolution;
out vec2 vUV;
out vec2 vPix;
void main()
{
  vUV = aUV;
  vPix = aPos;
  vec2 ndc = aPos / uResolution * 2.0 - 1.0;
  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
