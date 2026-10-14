// Trucos (códigos GameShark) del juego, leídos de assets/cheats/<serie>.cht (formato chtdb).
#pragma once
#include "i18n.h"

#include <string>
#include <vector>

struct Cheat
{
  std::string name;  // nombre original (clave para guardar si está activo)
  std::string label; // nombre traducido para mostrar
  std::string label_en; // nombre original, con los grupos "A\B" como "A · B"
  const std::string& Label() const { return LangEn() ? label_en : label; }
  std::string codes; // códigos en formato libretro: "AAAAAAAA VVVV+AAAAAAAA VVVV"
};

std::vector<Cheat> LoadCheats(const std::string& assets_dir, const std::string& serial);
