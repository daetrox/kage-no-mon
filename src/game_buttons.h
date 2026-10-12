// Iconos de botones dentro de Tenchu 2 según el mando que se esté usando.
//
// El juego sube a la VRAM una tira de iconos (✕ ○ □ △ de 16x16, 4 bits) y su paleta. El núcleo
// (parche kage_vram de SwanStation) las reconoce por su hash y las sustituye por los iconos que
// genera aquí el launcher: letras A/B/X/Y de Xbox o la tecla asignada en el teclado. Sólo se
// distribuyen estos dibujos propios, nunca las texturas del juego.
#pragma once
#include "config.h"

#include <cstdint>
#include <vector>

enum class GameButtonStyle
{
  Original, // iconos de PlayStation del propio juego
  Xbox,
  Keyboard,
};

// Registros para kage_vram_set_patches (vacío = iconos originales).
std::vector<uint8_t> BuildGameButtonPatches(GameButtonStyle style, const Config& cfg);
// Subidas que el núcleo debe vigilar, aunque empiecen sin parche: hash XXH3-128 y el sitio de la
// VRAM donde las pone siempre el juego (para reescribirlas tras cambiar de mando o cargar un estado).
struct GameUpload
{
  uint64_t hash_low, hash_high;
  uint32_t x, y;
};
std::vector<GameUpload> GameButtonUploads();
// Imagen RGBA 16x(16*5) de la tira generada (para pruebas).
std::vector<uint32_t> PreviewGameButtons(GameButtonStyle style, const Config& cfg);
