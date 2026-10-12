// Iconos de botones dentro de Tenchu 2 según el mando que se esté usando.
//
// Tenchu 2 dibuja los iconos ✕ ○ □ △ y START en varias texturas: la tira de los menús, el fondo
// del menú de objetos y los textos del curso de entrenamiento. El núcleo (parche kage_vram de
// SwanStation) reconoce esas subidas a la VRAM por su hash y cambia sus píxeles por los iconos que
// genera aquí el launcher: A/B/X/Y de Xbox o la tecla asignada. Sólo se distribuyen estos dibujos
// propios (y posiciones e índices de color), nunca las texturas del juego.
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

// Registros para kage_vram_set_patches (con Original, los que restauran los iconos del juego).
std::vector<uint8_t> BuildGameButtonPatches(GameButtonStyle style, const Config& cfg);
// Subidas que el núcleo debe vigilar, aunque empiecen sin parche: hash XXH3-128 y el sitio de la
// VRAM donde las pone siempre el juego (para reescribirlas tras cambiar de mando o cargar un estado).
struct GameUpload
{
  uint64_t hash_low, hash_high;
  uint32_t x, y;
  bool fixed; // el juego no usa nunca ese sitio para otra cosa
};
std::vector<GameUpload> GameButtonUploads();
// Imagen RGBA 16x(16*5) de la tira de los menús generada (para pruebas).
std::vector<uint32_t> PreviewGameButtons(GameButtonStyle style, const Config& cfg);
