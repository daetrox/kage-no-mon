// Trucos (códigos GameShark) del juego, leídos de assets/cheats/<serie>.cht (formato chtdb).
#pragma once
#include <string>
#include <vector>

struct Cheat
{
  std::string name;  // nombre original (clave para guardar si está activo)
  std::string label; // nombre traducido para mostrar
  std::string codes; // códigos en formato libretro: "AAAAAAAA VVVV+AAAAAAAA VVVV"
};

std::vector<Cheat> LoadCheats(const std::string& assets_dir, const std::string& serial);
