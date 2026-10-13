// Cámara libre para Tenchu 2 con el joystick derecho.
//
// El juego guarda la posición de la cámara en RAM y la suaviza frame a frame hacia su posición
// ideal, mirando siempre al personaje. Tras cada frame emulado se escribe ahí una posición en órbita
// alrededor del punto al que mira la cámara (la cabeza del personaje): el juego sigue encuadrándolo
// solo. No se toca el código del juego.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

class FreeCamera
{
public:
  // Direcciones para el disco cargado; false si no se conocen (la cámara libre no estará disponible).
  bool SetGame(const std::string& serial);
  bool Available() const { return m_cam != 0; }

  // Tras cada frame emulado. stick_x/stick_y en -1..1; look = L1 pulsado (vista en primera persona);
  // reset = R3 recién pulsado.
  void Frame(uint8_t* ram, size_t size, float stick_x, float stick_y, bool look, bool reset, float dt);
  void Reset() { m_active = false; }
  bool Active() const { return m_active; }

private:
  uint32_t m_cam = 0, m_target = 0; // offsets en la RAM (vectores de 3 enteros de 32 bits)
  bool m_active = false;
  float m_yaw = 0, m_pitch = 0, m_dist = 0;
  int32_t m_last_target[3] = {};
};
