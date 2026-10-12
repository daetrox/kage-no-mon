#include "game_buttons.h"

#include <SDL.h>
#include "libretro.h"

#include <cctype>
#include <cstring>
#include <string>

namespace {

// Subidas de Tenchu 2 (versión PAL; los iconos y su paleta son comunes a todos los idiomas).
// Tira de 5 iconos de 16x16 a 4 bits: 4x80 palabras en VRAM (1000,256).
constexpr uint64_t kIconsLo = 0xa2dd40cfc0822658ull, kIconsHi = 0x63040010f127bbf2ull;
constexpr uint16_t kIconsW = 4, kIconsH = 80;
// Su paleta: 16x1 en VRAM (1008,511).
constexpr uint64_t kClutLo = 0xb32eb74bb9699d93ull, kClutHi = 0x0bbf484bedae0be1ull;
constexpr uint16_t kClutW = 16, kClutH = 1;

// Índices de la paleta. 8..e son grises (de claro a oscuro) y f es transparente; el resto se
// recolorea: 0 (rosa de □) pasa a amarillo, 1 a blanco y 6 (naranja de ○) a rojo. El 2 (azul de
// ✕) y el 4 (verde de △) se quedan como están.
constexpr uint8_t kYellow = 0x0, kWhite = 0x1, kBlue = 0x2, kGreen = 0x4, kRed = 0x6;
constexpr uint8_t kGreyLight = 0x8, kGreyDark = 0xe, kClear = 0xf;

// Iconos de la tira (el 0 es la flecha de la cruceta y no se toca) y el botón de PS1 que muestran.
constexpr int kSlots = 4;
constexpr unsigned kSlotButton[kSlots] = {
  RETRO_DEVICE_ID_JOYPAD_A, // ○
  RETRO_DEVICE_ID_JOYPAD_B, // ✕
  RETRO_DEVICE_ID_JOYPAD_Y, // □
  RETRO_DEVICE_ID_JOYPAD_X, // △
};

// Fuente de 5x7 (letras grandes) y de 3x5 (etiquetas de varias letras), en mayúsculas y cifras.
const char* const kFont5[36][7] = {
  {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}, // A
  {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}, // B
  {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}, // C
  {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}, // D
  {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}, // E
  {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}, // F
  {"01110", "10001", "10000", "10111", "10001", "10001", "01111"}, // G
  {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}, // H
  {"01110", "00100", "00100", "00100", "00100", "00100", "01110"}, // I
  {"00111", "00010", "00010", "00010", "00010", "10010", "01100"}, // J
  {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}, // K
  {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}, // L
  {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}, // M
  {"10001", "10001", "11001", "10101", "10011", "10001", "10001"}, // N
  {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}, // O
  {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}, // P
  {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}, // Q
  {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}, // R
  {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}, // S
  {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}, // T
  {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}, // U
  {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}, // V
  {"10001", "10001", "10001", "10101", "10101", "10101", "01010"}, // W
  {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}, // X
  {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}, // Y
  {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}, // Z
  {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}, // 0
  {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}, // 1
  {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}, // 2
  {"11111", "00010", "00100", "00010", "00001", "10001", "01110"}, // 3
  {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}, // 4
  {"11111", "10000", "11110", "00001", "00001", "10001", "01110"}, // 5
  {"00110", "01000", "10000", "11110", "10001", "10001", "01110"}, // 6
  {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}, // 7
  {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}, // 8
  {"01110", "10001", "10001", "01111", "00001", "00010", "01100"}, // 9
};
const char* const kFont3[36][5] = {
  {"010", "101", "111", "101", "101"}, {"110", "101", "110", "101", "110"}, {"011", "100", "100", "100", "011"},
  {"110", "101", "101", "101", "110"}, {"111", "100", "110", "100", "111"}, {"111", "100", "110", "100", "100"},
  {"011", "100", "101", "101", "011"}, {"101", "101", "111", "101", "101"}, {"111", "010", "010", "010", "111"},
  {"001", "001", "001", "101", "010"}, {"101", "101", "110", "101", "101"}, {"100", "100", "100", "100", "111"},
  {"101", "111", "111", "101", "101"}, {"110", "101", "101", "101", "101"}, {"010", "101", "101", "101", "010"},
  {"110", "101", "110", "100", "100"}, {"010", "101", "101", "110", "011"}, {"110", "101", "110", "101", "101"},
  {"011", "100", "010", "001", "110"}, {"111", "010", "010", "010", "010"}, {"101", "101", "101", "101", "111"},
  {"101", "101", "101", "101", "010"}, {"101", "101", "111", "111", "101"}, {"101", "101", "010", "101", "101"},
  {"101", "101", "010", "010", "010"}, {"111", "001", "010", "100", "111"}, {"111", "101", "101", "101", "111"},
  {"010", "110", "010", "010", "111"}, {"110", "001", "010", "100", "111"}, {"110", "001", "010", "001", "110"},
  {"101", "101", "111", "001", "001"}, {"111", "100", "110", "001", "110"}, {"011", "100", "110", "101", "010"},
  {"111", "001", "010", "010", "010"}, {"010", "101", "010", "101", "010"}, {"010", "101", "011", "001", "110"},
};

int GlyphIndex(char c)
{
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= '0' && c <= '9')
    return 26 + (c - '0');
  return -1;
}

using Icon = uint8_t[16][16];

void Put(Icon& ic, int x, int y, uint8_t c)
{
  if (x >= 0 && x < 16 && y >= 0 && y < 16)
    ic[y][x] = c;
}

// Letra grande en negrita (5x7 desplazada un píxel: 6x7), con sombra.
void DrawBigLetter(Icon& ic, char ch, uint8_t color)
{
  const int g = GlyphIndex(ch);
  if (g < 0)
    return;
  const int ox = 5, oy = 4;
  for (int pass = 0; pass < 2; pass++) // 0: sombra (un píxel abajo a la derecha), 1: letra
  {
    const int s = pass ? 0 : 1;
    for (int y = 0; y < 7; y++)
      for (int x = 0; x < 5; x++)
        if (kFont5[g][y][x] == '1')
          for (int b = 0; b < 2; b++)
            Put(ic, ox + x + b + s, oy + y + s, pass ? color : kGreyDark);
  }
}

// Texto corto (hasta 3 caracteres) con la fuente de 3x5, centrado.
void DrawSmallText(Icon& ic, const std::string& text, uint8_t color, int cy)
{
  const int n = static_cast<int>(std::min<size_t>(text.size(), 3));
  const int w = n * 4 - 1;
  int x0 = (16 - w) / 2;
  for (int i = 0; i < n; i++, x0 += 4)
  {
    const int g = GlyphIndex(text[i]);
    if (g < 0)
      continue;
    for (int y = 0; y < 5; y++)
      for (int x = 0; x < 3; x++)
        if (kFont3[g][y][x] == '1')
          Put(ic, x0 + x, cy + y, color);
  }
}

// Flecha (cruceta o teclas de dirección): 0 arriba, 1 abajo, 2 izquierda, 3 derecha.
void DrawArrow(Icon& ic, int dir, uint8_t color)
{
  for (int i = 0; i < 4; i++)
    for (int j = -i; j <= i; j++)
    {
      const int a = 5 + i, b = 8 + j; // punta hacia arriba en (8,5)
      switch (dir)
      {
        case 0: Put(ic, b, a, color); Put(ic, b - 1 + (j >= 0), a, color); break;
        case 1: Put(ic, b, 15 - a, color); Put(ic, b - 1 + (j >= 0), 15 - a, color); break;
        case 2: Put(ic, a, b, color); Put(ic, a, b - 1 + (j >= 0), color); break;
        case 3: Put(ic, 15 - a, b, color); Put(ic, 15 - a, b - 1 + (j >= 0), color); break;
      }
    }
}

// Disco gris con sombreado (botones de cara del mando).
void DrawDisc(Icon& ic)
{
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++)
    {
      const float dx = x + 0.5f - 8, dy = y + 0.5f - 8;
      const float d2 = dx * dx + dy * dy;
      if (d2 > 7.6f * 7.6f)
        continue;
      if (d2 > 6.6f * 6.6f)
        ic[y][x] = kGreyDark;
      else
        ic[y][x] = static_cast<uint8_t>(kGreyLight + std::min(4, (x + y) / 7));
    }
}

// Tecla (o botón que no es de cara): rectángulo redondeado oscuro con borde blanco.
void DrawKeycap(Icon& ic)
{
  for (int y = 1; y < 15; y++)
    for (int x = 0; x < 16; x++)
    {
      const bool corner = (x == 0 || x == 15) && (y == 1 || y == 14);
      if (corner)
        continue;
      const bool edge = x == 0 || x == 15 || y == 1 || y == 14 ||
                        ((x == 1 || x == 14) && (y == 2 || y == 13));
      ic[y][x] = edge ? kWhite : static_cast<uint8_t>(kGreyDark - 1);
    }
}

// Etiqueta corta de una tecla: un carácter, o hasta 3 letras; dirección = flecha.
struct Label
{
  std::string text;
  int arrow = -1;
};

Label KeyLabel(int scancode)
{
  switch (scancode)
  {
    case SDL_SCANCODE_UP: return {"", 0};
    case SDL_SCANCODE_DOWN: return {"", 1};
    case SDL_SCANCODE_LEFT: return {"", 2};
    case SDL_SCANCODE_RIGHT: return {"", 3};
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return {"ENT"};
    case SDL_SCANCODE_SPACE: return {"SPC"};
    case SDL_SCANCODE_ESCAPE: return {"ESC"};
    case SDL_SCANCODE_BACKSPACE: return {"DEL"};
    case SDL_SCANCODE_TAB: return {"TAB"};
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT: return {"MAY"};
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: return {"CTL"};
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT: return {"ALT"};
    default: break;
  }
  std::string out;
  if (const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode)))
    for (const char* p = name; *p && out.size() < 3; p++)
      if (std::isalnum(static_cast<unsigned char>(*p)))
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
  return {out.empty() ? "?" : out};
}

// Botones de cara (A/B/X/Y): devuelve su color en `face_color`; si no, -1.
Label PadLabel(int bind, int* face_color)
{
  *face_color = -1;
  if (bind >= kPadAxisBase)
    return {(bind - kPadAxisBase) == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? "LT" : "RT"};
  switch (bind)
  {
    case SDL_CONTROLLER_BUTTON_A: *face_color = kGreen; return {"A"};
    case SDL_CONTROLLER_BUTTON_B: *face_color = kRed; return {"B"};
    case SDL_CONTROLLER_BUTTON_X: *face_color = kBlue; return {"X"};
    case SDL_CONTROLLER_BUTTON_Y: *face_color = kYellow; return {"Y"};
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return {"LB"};
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return {"RB"};
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return {"LS"};
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return {"RS"};
    case SDL_CONTROLLER_BUTTON_BACK: return {"VW"};
    case SDL_CONTROLLER_BUTTON_START: return {"MEN"};
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return {"", 0};
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return {"", 1};
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return {"", 2};
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return {"", 3};
    default: return {"?"};
  }
}

void DrawSlot(Icon& ic, GameButtonStyle style, const Config& cfg, unsigned button)
{
  std::memset(ic, kClear, sizeof(Icon));
  int face = -1;
  const Label l = style == GameButtonStyle::Keyboard ? KeyLabel(cfg.key_bind[button])
                                                     : PadLabel(cfg.pad_bind[button], &face);
  if (face >= 0)
  {
    DrawDisc(ic);
    DrawBigLetter(ic, l.text[0], static_cast<uint8_t>(face));
    return;
  }
  DrawKeycap(ic);
  if (l.arrow >= 0)
    DrawArrow(ic, l.arrow, kWhite);
  else if (l.text.size() == 1)
    DrawBigLetter(ic, l.text[0], kWhite);
  else
    DrawSmallText(ic, l.text, kWhite, 6);
}

void Push16(std::vector<uint8_t>& out, uint16_t v)
{
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}

void Push64(std::vector<uint8_t>& out, uint64_t v)
{
  for (int i = 0; i < 8; i++)
    out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void Header(std::vector<uint8_t>& out, uint64_t lo, uint64_t hi, uint16_t w, uint16_t h, uint16_t rects)
{
  out.insert(out.end(), {'K', 'V', 'P', '1'});
  Push64(out, lo);
  Push64(out, hi);
  Push16(out, w);
  Push16(out, h);
  Push16(out, rects);
}

// Color de PS1 (15 bits, con el bit de semitransparencia como en la paleta original).
uint16_t Rgb15(int r, int g, int b)
{
  return static_cast<uint16_t>((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000);
}

void BuildIcons(GameButtonStyle style, const Config& cfg, Icon (&icons)[kSlots])
{
  for (int i = 0; i < kSlots; i++)
    DrawSlot(icons[i], style, cfg, kSlotButton[i]);
}

} // namespace

std::vector<GameUpload> GameButtonUploads()
{
  return {{kIconsLo, kIconsHi, 1000, 256}, {kClutLo, kClutHi, 1008, 511}};
}

std::vector<uint8_t> BuildGameButtonPatches(GameButtonStyle style, const Config& cfg)
{
  std::vector<uint8_t> out;
  if (style == GameButtonStyle::Original)
    return out;

  // Iconos 1..4 (filas 16..79): se sustituyen enteros.
  Icon icons[kSlots];
  BuildIcons(style, cfg, icons);
  Header(out, kIconsLo, kIconsHi, kIconsW, kIconsH, 1);
  for (uint16_t v : {uint16_t(0), uint16_t(16), uint16_t(kIconsW), uint16_t(kSlots * 16)})
    Push16(out, v);
  for (int i = 0; i < kSlots; i++)
    for (int y = 0; y < 16; y++)
      for (int wx = 0; wx < 4; wx++)
      {
        uint16_t word = 0;
        for (int k = 0; k < 4; k++)
          word |= static_cast<uint16_t>(icons[i][y][wx * 4 + k] << (4 * k));
        Push16(out, word);
      }

  // Paleta: sólo los colores recoloreados (amarillo y blanco en 0..1, rojo en 6).
  Header(out, kClutLo, kClutHi, kClutW, kClutH, 2);
  for (uint16_t v : {uint16_t(0), uint16_t(0), uint16_t(2), uint16_t(1)})
    Push16(out, v);
  Push16(out, Rgb15(232, 200, 56));
  Push16(out, Rgb15(248, 248, 248));
  for (uint16_t v : {uint16_t(6), uint16_t(0), uint16_t(1), uint16_t(1)})
    Push16(out, v);
  Push16(out, Rgb15(224, 64, 56));
  return out;
}

std::vector<uint32_t> PreviewGameButtons(GameButtonStyle style, const Config& cfg)
{
  // Colores aproximados de la paleta del juego, sólo para la vista previa.
  static const uint32_t pal[16] = {0xFF38C8E8, 0xFFF8F8F8, 0xFFC89088, 0xFF686860, 0xFF88D048, 0xFF78B048,
                                   0xFF3840E0, 0xFF386898, 0xFF585858, 0xFF505050, 0xFF484848, 0xFF404040,
                                   0xFF383838, 0xFF282828, 0xFF202020, 0x00000000};
  Icon icons[kSlots];
  BuildIcons(style, cfg, icons);
  std::vector<uint32_t> img(16 * 16 * kSlots);
  for (int i = 0; i < kSlots; i++)
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++)
        img[(i * 16 + y) * 16 + x] = pal[icons[i][y][x]];
  return img;
}
