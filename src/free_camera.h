// Cámara libre para Tenchu 2 con el joystick derecho.
//
// La cámara normal del juego se calcula a partir de una tabla de vectores (posición de la cámara y
// del punto al que mira, relativos al personaje) que se rotan con la orientación del personaje;
// después el juego comprueba la colisión con los muros y suaviza el movimiento. La cámara libre
// gira esos vectores de la tabla, así que el juego coloca la cámara en órbita con su propia
// colisión. Con cualquier otro modo de cámara (apuntar, primera persona) se restaura la tabla.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class FreeCamera
{
public:
  // Direcciones para el disco cargado; false si no se conocen (la cámara libre no estará disponible).
  bool SetGame(const std::string& serial);
  bool Available() const { return m_table_begin != 0; }

  // Tras cada frame emulado. stick_x/stick_y en -1..1; reset = R3 recién pulsado.
  void Frame(uint8_t* ram, size_t size, float stick_x, float stick_y, bool reset, float dt);
  void SetSpeed(float factor) { m_speed = factor; } // 1 = velocidad normal
  bool Active() const { return m_active; }
  // Antes de guardar un estado: la tabla vuelve a su valor original (se vuelve a girar en el
  // siguiente frame), para que ningún estado guardado la lleve modificada.
  void Restore(uint8_t* ram);

private:
  void Apply(uint8_t* ram, float yaw, float pitch);

  // Offsets en la RAM.
  uint32_t m_table_begin = 0, m_table_end = 0; // tabla de vectores de la cámara
  uint32_t m_mode = 0;                          // modo de cámara del juego (16 bits, 0 = normal)
  uint32_t m_player = 0;                        // puntero al personaje
  uint32_t m_target = 0;                        // punto al que mira la cámara (3 enteros de 32 bits)

  std::vector<int16_t> m_original; // copia de la tabla sin tocar
  bool m_modified = false;         // la tabla de la RAM está girada
  bool m_active = false;
  float m_world_yaw = 0, m_pitch = 0; // orientación elegida (radianes; el giro, respecto al mundo)
  float m_speed = 1.0f;
  int32_t m_last_target[3] = {};
};
