// Cámara libre para Tenchu 2 con el joystick derecho.
//
// La cámara normal del juego parte de una tabla de vectores (posición de la cámara y del punto al
// que mira, relativos al personaje) que se rotan con la orientación del personaje; luego el juego
// comprueba la colisión con el escenario y acerca la cámara a esa posición deseada poco a poco.
//
// La cámara libre gira esos vectores, así que el juego calcula su posición deseada (con colisión)
// en la dirección elegida, pero congela su suavizado: la cámara la coloca el launcher, en órbita
// alrededor de la cabeza del personaje y mirándola (el juego adelanta su punto de mira al correr),
// y de la posición deseada del juego sólo toma la distancia (se acerca deprisa ante un muro y se
// aleja despacio). Con cualquier otro modo de cámara (apuntar, primera persona, nadar…) se
// restaura todo y manda el juego.
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

  // Tras cada frame emulado. stick_x/stick_y en -1..1; reset = R3 recién pulsado; keep = mantener
  // la cámara libre aunque no se toque el joystick (controles 360°).
  void Frame(uint8_t* ram, size_t size, float stick_x, float stick_y, bool reset, bool keep, float dt);
  // Controles 360° (tras Frame): orienta al personaje hacia la dirección pedida (move_x/move_y en
  // -1..1, relativa a la cámara: arriba = hacia dentro de la pantalla). Sólo con la cámara libre
  // activa y el personaje en un modo de movimiento normal; si no, Steerable() es false y el juego
  // recibe los controles originales.
  // guard = el launcher mantiene la guardia automática (arma fuera, quieto): el juego sólo la tiene
  // andando hacia atrás (acción 0x104), así que se deja al personaje en el sitio.
  void Steer(uint8_t* ram, size_t size, float move_x, float move_y, bool guard, float dt);
  bool Steerable() const { return m_steerable; }
  // Con el arma desenvainada (según el último Steer): quieto, el personaje se pone en guardia.
  bool Armed() const { return m_armed; }
  void SetSpeed(float factor) { m_speed = factor; } // 1 = velocidad normal
  bool Active() const { return m_active; }
  // Devuelve la tabla y el suavizado del juego a su estado original (antes de guardar un estado, al
  // ceder la cámara al juego…); se vuelven a modificar en el siguiente frame si hace falta.
  void Restore(uint8_t* ram);

private:
  void Apply(uint8_t* ram, float yaw, float pitch);

  // Offsets en la RAM.
  uint32_t m_table_begin = 0, m_table_end = 0; // tabla de vectores de la cámara
  uint32_t m_mode = 0;                          // modo de cámara del juego (16 bits)
  uint32_t m_player = 0;                        // puntero al personaje
  uint32_t m_cam = 0;                           // posición de la cámara (3 enteros de 32 bits)
  uint32_t m_target = 0;                        // punto al que mira la cámara (justo detrás)
  uint32_t m_smooth = 0;                        // parámetros del suavizado de la posición
  uint32_t m_smooth_target = 0;                 // y del punto al que mira

  std::vector<int16_t> m_original; // copia de la tabla sin tocar
  int16_t m_smooth_original[3] = {}, m_smooth_target_original[3] = {};
  bool m_modified = false;   // tabla girada en la RAM
  bool m_smooth_set = false; // suavizado congelado en la RAM

  bool m_active = false;
  bool m_returning = false; // tras R3: girando hasta quedar detrás del personaje
  bool m_steerable = false; // controles 360° aplicables en el frame siguiente
  bool m_armed = false;     // el personaje tiene el arma desenvainada
  bool m_guard_hold = false; // guardia automática en curso: posición fijada
  int32_t m_guard_pos[3] = {};
  float m_steer_yaw = 0;    // giro actual del personaje con los controles 360° (radianes)
  float m_world_yaw = 0, m_pitch = 0;       // orientación elegida (radianes)
  float m_dist = 0, m_height = 0;           // distancia horizontal y altura respecto a la cabeza
  float m_game_dist = 0, m_game_height = 0; // las que quiere el juego (con colisión)
  float m_ignore_game = 0;                  // segundos sin tener en cuenta esa distancia
  int16_t m_last_mode = 0;
  float m_head = 0;                         // altura del punto de mira sobre la posición del personaje
  int32_t m_written[6] = {};                // última posición escrita (cámara y punto de mira)
  bool m_have_written = false;
  float m_speed = 1.0f;
  int32_t m_last_target[3] = {}; // posición del personaje en el frame anterior
};
