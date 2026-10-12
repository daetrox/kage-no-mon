#include "renderer.h"

#include <SDL.h>
#include <algorithm>
#include <fstream>
#include <sstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "ext/stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "ext/stb_image.h"

static void PushQuad(std::vector<float>& v, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                     float v1)
{
  const float q[] = {x0, y0, u0, v0, x1, y0, u1, v0, x1, y1, u1, v1,
                     x0, y0, u0, v0, x1, y1, u1, v1, x0, y1, u0, v1};
  v.insert(v.end(), std::begin(q), std::end(q));
}

std::vector<uint32_t> DecodeUtf8(const std::string& s)
{
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();)
  {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp = c;
    int extra = 0;
    if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
    else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
    else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
    i++;
    for (int k = 0; k < extra && i < s.size(); k++, i++)
      cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    out.push_back(cp);
  }
  return out;
}

static bool ReadFile(const std::string& path, std::string* out)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  std::stringstream ss;
  ss << in.rdbuf();
  *out = ss.str();
  return true;
}

GLuint Renderer::Compile(const std::string& vs_path, const std::string& fs_path, std::string* error)
{
  std::string vs_src, fs_src, common;
  if (!ReadFile(vs_path, &vs_src) || !ReadFile(fs_path, &fs_src) ||
      !ReadFile(m_assets + "/shaders/common.glsl", &common))
  {
    *error = "No se encuentran los shaders en " + m_assets + "/shaders";
    return 0;
  }
  // Inserta las funciones comunes (ruido, etc.) tras la línea #version.
  const size_t nl = fs_src.find('\n');
  fs_src = fs_src.substr(0, nl + 1) + common + fs_src.substr(nl + 1);

  auto compile = [&](GLenum type, const std::string& src, const std::string& name) -> GLuint {
    GLuint sh = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
      char log[4096];
      glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
      *error = "Error compilando " + name + ":\n" + log;
      glDeleteShader(sh);
      return 0;
    }
    return sh;
  };

  GLuint vs = compile(GL_VERTEX_SHADER, vs_src, vs_path);
  if (!vs)
    return 0;
  GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src, fs_path);
  if (!fs)
  {
    glDeleteShader(vs);
    return 0;
  }
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    char log[4096];
    glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
    *error = std::string("Error enlazando shader:\n") + log;
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

bool Renderer::ReloadShaders(std::string* error)
{
  const std::string dir = m_assets + "/shaders/";
  GLuint scene = Compile(dir + "quad.vert", dir + "scene.frag", error);
  if (!scene)
    return false;
  GLuint ui = Compile(dir + "quad.vert", dir + "ui.frag", error);
  if (!ui)
  {
    glDeleteProgram(scene);
    return false;
  }
  GLuint bg = Compile(dir + "quad.vert", dir + "background.frag", error);
  if (!bg)
  {
    glDeleteProgram(scene);
    glDeleteProgram(ui);
    return false;
  }
  if (m_bg_prog)
    glDeleteProgram(m_bg_prog);
  m_bg_prog = bg;
  if (m_scene_prog)
    glDeleteProgram(m_scene_prog);
  if (m_ui_prog)
    glDeleteProgram(m_ui_prog);
  m_scene_prog = scene;
  m_ui_prog = ui;
  return true;
}

bool Renderer::LoadFont(Font& font, const std::string& path, float size, const std::vector<uint32_t>& codepoints,
                        std::string* error)
{
  std::string data;
  if (!ReadFile(path, &data))
  {
    *error = "No se encuentra la fuente " + path;
    return false;
  }
  const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());

  stbtt_fontinfo info;
  stbtt_InitFont(&info, bytes, stbtt_GetFontOffsetForIndex(bytes, 0));
  int ascent, descent, gap;
  stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
  font.ascent = ascent * stbtt_ScaleForPixelHeight(&info, size);
  font.bake_size = size;

  const int atlas = 2048;
  std::vector<unsigned char> pixels(atlas * atlas);
  std::vector<stbtt_packedchar> packed(codepoints.size());
  std::vector<int> cps(codepoints.begin(), codepoints.end());

  stbtt_pack_context pc;
  stbtt_PackBegin(&pc, pixels.data(), atlas, atlas, 0, 2, nullptr);
  stbtt_PackSetOversampling(&pc, 2, 2);
  stbtt_pack_range range = {};
  range.font_size = size;
  range.array_of_unicode_codepoints = cps.data();
  range.num_chars = static_cast<int>(cps.size());
  range.chardata_for_range = packed.data();
  stbtt_PackFontRanges(&pc, bytes, 0, &range, 1);
  stbtt_PackEnd(&pc);

  for (size_t i = 0; i < codepoints.size(); i++)
  {
    const stbtt_packedchar& p = packed[i];
    if (p.x1 == p.x0 && p.xadvance == 0)
      continue;
    Glyph g;
    g.x0 = p.xoff;
    g.y0 = p.yoff;
    g.x1 = p.xoff2;
    g.y1 = p.yoff2;
    g.u0 = p.x0 / static_cast<float>(atlas);
    g.v0 = p.y0 / static_cast<float>(atlas);
    g.u1 = p.x1 / static_cast<float>(atlas);
    g.v1 = p.y1 / static_cast<float>(atlas);
    g.advance = p.xadvance;
    font.glyphs[codepoints[i]] = g;
  }

  glGenTextures(1, &font.texture);
  glBindTexture(GL_TEXTURE_2D, font.texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlas, atlas, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return true;
}

bool Renderer::Init(const std::string& assets_dir, std::string* error)
{
  m_assets = assets_dir;

  glGenVertexArrays(1, &m_vao);
  glGenBuffers(1, &m_vbo);
  glBindVertexArray(m_vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

  if (!ReloadShaders(error))
    return false;

  // Latín básico + Latin-1 (acentos, ñ, ¿, ¡, ·).
  std::vector<uint32_t> latin;
  for (uint32_t c = 32; c < 127; c++) latin.push_back(c);
  for (uint32_t c = 160; c < 256; c++) latin.push_back(c);
  latin.push_back(0x2026); // …
  latin.push_back(0x2014); // —

  const std::string fonts = m_assets + "/fonts/";
  if (!LoadFont(logo, fonts + "Shojumaru-Regular.ttf", 150, latin, error)) return false;
  if (!LoadFont(ui, fonts + "Oswald[wght].ttf", 56, latin, error)) return false;
  if (!LoadFont(script, fonts + "KaushanScript-Regular.ttf", 60, latin, error)) return false;
  if (!LoadFont(brush, fonts + "PermanentMarker-Regular.ttf", 72, latin, error)) return false;
  // Sólo los kanji decorativos que usa la interfaz.
  const std::vector<uint32_t> kanji_cps = DecodeUtf8("忍影闇火城天命誅刃盤絵音操術");
  if (!LoadFont(kanji, fonts + "YujiSyuku-Regular.ttf", 180, kanji_cps, error)) return false;
  return true;
}

void Renderer::Shutdown()
{
  for (Font* f : {&logo, &ui, &script, &kanji, &brush})
    if (f->texture)
      glDeleteTextures(1, &f->texture);
  glDeleteProgram(m_scene_prog);
  glDeleteProgram(m_ui_prog);
  glDeleteBuffers(1, &m_vbo);
  glDeleteVertexArrays(1, &m_vao);
}

void Renderer::BeginFrame(int width, int height, float time)
{
  m_w = width;
  m_h = height;
  m_time = time;
  m_clips.clear();
  glViewport(0, 0, width, height);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(m_vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
}

void Renderer::SetCommonUniforms(GLuint prog)
{
  glUseProgram(prog);
  glUniform2f(glGetUniformLocation(prog, "uResolution"), static_cast<float>(m_w), static_cast<float>(m_h));
  glUniform1f(glGetUniformLocation(prog, "uTime"), m_time);
}

void Renderer::Flush(const std::vector<float>& verts)
{
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 4));
}


void Renderer::DrawScene(float mouse_x, float mouse_y, float intensity)
{
  SetCommonUniforms(m_scene_prog);
  glUniform2f(glGetUniformLocation(m_scene_prog, "uMouse"), mouse_x, mouse_y);
  glUniform1f(glGetUniformLocation(m_scene_prog, "uIntensity"), intensity);
  m_scratch.clear();
  PushQuad(m_scratch, 0, 0, static_cast<float>(m_w), static_cast<float>(m_h), 0, 0, 1, 1);
  Flush(m_scratch);
}


bool Renderer::LoadTexture(const std::string& path, Texture* tex, bool mipmaps)
{
  int w, h, n;
  unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
  if (!data)
  {
    SDL_Log("No se pudo cargar %s", path.c_str());
    return false;
  }
  glGenTextures(1, &tex->id);
  glBindTexture(GL_TEXTURE_2D, tex->id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  stbi_image_free(data);
  if (mipmaps)
    glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  tex->w = w;
  tex->h = h;
  return true;
}

void Renderer::DrawBackground(const Texture& bg, const Texture& fire_mask, float x, float y, float w, float h,
                              float intensity)
{
  SetCommonUniforms(m_bg_prog);
  glUniform4f(glGetUniformLocation(m_bg_prog, "uImgRect"), x, y, w, h);
  glUniform1f(glGetUniformLocation(m_bg_prog, "uIntensity"), intensity);
  glUniform1i(glGetUniformLocation(m_bg_prog, "uBg"), 0);
  glUniform1i(glGetUniformLocation(m_bg_prog, "uMask"), 1);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, fire_mask.id);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, bg.id);
  m_scratch.clear();
  PushQuad(m_scratch, 0, 0, static_cast<float>(m_w), static_cast<float>(m_h), 0, 0, 1, 1);
  Flush(m_scratch);
}

void Renderer::Sprite(const Texture& tex, float x, float y, float w, float h, Color tint, bool gray)
{
  SetCommonUniforms(m_ui_prog);
  glUniform1i(glGetUniformLocation(m_ui_prog, "uMode"), static_cast<int>(gray ? UiMode::SpriteGray : UiMode::Sprite));
  glUniform4f(glGetUniformLocation(m_ui_prog, "uColor"), tint.r, tint.g, tint.b, tint.a);
  glUniform4f(glGetUniformLocation(m_ui_prog, "uRect"), x, y, w, h);
  glUniform1i(glGetUniformLocation(m_ui_prog, "uTex"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex.id);
  m_scratch.clear();
  PushQuad(m_scratch, x, y, x + w, y + h, 0, 0, 1, 1);
  Flush(m_scratch);
}

void Renderer::Rect(float x, float y, float w, float h, UiMode mode, Color c, Color c2, float p0, float p1, float p2,
                    float p3)
{
  SetCommonUniforms(m_ui_prog);
  glUniform1i(glGetUniformLocation(m_ui_prog, "uMode"), static_cast<int>(mode));
  glUniform4f(glGetUniformLocation(m_ui_prog, "uColor"), c.r, c.g, c.b, c.a);
  glUniform4f(glGetUniformLocation(m_ui_prog, "uColor2"), c2.r, c2.g, c2.b, c2.a);
  glUniform4f(glGetUniformLocation(m_ui_prog, "uRect"), x, y, w, h);
  glUniform4f(glGetUniformLocation(m_ui_prog, "uParam"), p0, p1, p2, p3);
  m_scratch.clear();
  PushQuad(m_scratch, x, y, x + w, y + h, 0, 0, 1, 1);
  Flush(m_scratch);
}

void Renderer::DrawIcon(Icon icon, float x, float y, float size, Color c, float p1)
{
  Rect(x, y, size, size, UiMode::Icon, c, {0, 0, 0, 0}, static_cast<float>(icon), p1);
}

void Renderer::Image(GLuint tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                     bool smooth)
{
  SetCommonUniforms(m_ui_prog);
  glUniform1i(glGetUniformLocation(m_ui_prog, "uMode"), static_cast<int>(UiMode::Image));
  glUniform4f(glGetUniformLocation(m_ui_prog, "uColor"), 1, 1, 1, 1);
  glUniform4f(glGetUniformLocation(m_ui_prog, "uRect"), x, y, w, h);
  glUniform1i(glGetUniformLocation(m_ui_prog, "uTex"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  const GLint filter = smooth ? GL_LINEAR : GL_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  m_scratch.clear();
  PushQuad(m_scratch, x, y, x + w, y + h, u0, v0, u1, v1);
  Flush(m_scratch);
}

void Renderer::TextCentered(const Font& font, const std::string& text, float cx, float cy, float size, Color c)
{
  const float scale = size / font.bake_size;
  float pen = 0, x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (uint32_t cp : DecodeUtf8(text))
  {
    auto it = font.glyphs.find(cp);
    if (it == font.glyphs.end())
      continue;
    const Glyph& g = it->second;
    x0 = std::min(x0, pen + g.x0 * scale);
    x1 = std::max(x1, pen + g.x1 * scale);
    y0 = std::min(y0, g.y0 * scale);
    y1 = std::max(y1, g.y1 * scale);
    pen += g.advance * scale;
  }
  if (x1 < x0)
    return;
  // Text() dibuja con la línea base en y + ascent; se compensa para centrar la caja.
  const float x = cx - (x0 + x1) / 2;
  const float y = cy - (y0 + y1) / 2 - font.ascent * scale;
  Text(font, text, x, y, size, c);
}

void Renderer::ApplyClip()
{
  if (m_clips.empty())
  {
    glDisable(GL_SCISSOR_TEST);
    return;
  }
  const ClipRect& c = m_clips.back();
  glEnable(GL_SCISSOR_TEST);
  glScissor(static_cast<GLint>(c.x0), static_cast<GLint>(m_h - c.y1), static_cast<GLsizei>(std::max(0.0f, c.x1 - c.x0)),
            static_cast<GLsizei>(std::max(0.0f, c.y1 - c.y0)));
}

void Renderer::SetClip(float x, float y, float w, float h)
{
  ClipRect c{x, y, x + w, y + h};
  if (!m_clips.empty())
  {
    const ClipRect& p = m_clips.back();
    c = {std::max(c.x0, p.x0), std::max(c.y0, p.y0), std::min(c.x1, p.x1), std::min(c.y1, p.y1)};
  }
  m_clips.push_back(c);
  ApplyClip();
}

void Renderer::ClearClip()
{
  if (!m_clips.empty())
    m_clips.pop_back();
  ApplyClip();
}

float Renderer::TextWidth(const Font& font, const std::string& text, float size) const
{
  const float scale = size / font.bake_size;
  float w = 0;
  for (uint32_t cp : DecodeUtf8(text))
  {
    auto it = font.glyphs.find(cp);
    if (it != font.glyphs.end())
      w += it->second.advance * scale;
  }
  return w;
}

float Renderer::Text(const Font& font, const std::string& text, float x, float y, float size, Color c, Align align,
                     Color c2, float effect)
{
  const float scale = size / font.bake_size;
  const float width = TextWidth(font, text, size);
  if (align == Align::Center)
    x -= width * 0.5f;
  else if (align == Align::Right)
    x -= width;

  // `y` es la parte superior de la línea; se convierte a línea base.
  const float baseline = y + font.ascent * scale;
  m_scratch.clear();
  float pen = x;
  for (uint32_t cp : DecodeUtf8(text))
  {
    auto it = font.glyphs.find(cp);
    if (it == font.glyphs.end())
      continue;
    const Glyph& g = it->second;
    PushQuad(m_scratch, pen + g.x0 * scale, baseline + g.y0 * scale, pen + g.x1 * scale, baseline + g.y1 * scale,
             g.u0, g.v0, g.u1, g.v1);
    pen += g.advance * scale;
  }
  if (m_scratch.empty())
    return width;

  SetCommonUniforms(m_ui_prog);
  glUniform1i(glGetUniformLocation(m_ui_prog, "uMode"), static_cast<int>(UiMode::Text));
  glUniform4f(glGetUniformLocation(m_ui_prog, "uColor"), c.r, c.g, c.b, c.a);
  glUniform4f(glGetUniformLocation(m_ui_prog, "uColor2"), c2.r, c2.g, c2.b, c2.a);
  glUniform4f(glGetUniformLocation(m_ui_prog, "uRect"), x, y, width, size);
  glUniform4f(glGetUniformLocation(m_ui_prog, "uParam"), effect, 0, 0, 0);
  glUniform1i(glGetUniformLocation(m_ui_prog, "uTex"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, font.texture);
  Flush(m_scratch);
  return width;
}
