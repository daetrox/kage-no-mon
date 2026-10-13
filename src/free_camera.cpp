#include "free_camera.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct GameCamera
{
  const char* serial;
  uint32_t table_begin, table_end; // entradas de 32 bytes: 4 SVECTOR relativos al personaje
  uint32_t mode;                   // modo de cámara (16 bits)
  uint32_t player;                 // puntero al personaje (+0x30 → su rotación; giro en +2)
  uint32_t cam;                    // posición de la cámara; justo detrás, el punto al que mira
  uint32_t smooth;                 // suavizado de la posición (0x80051B10): desplazamiento,
                                   // aceleración, velocidad y la diferencia con la posición deseada
  uint32_t smooth_target;          // suavizado del punto de mira (mismo formato)
};

// Localizadas con volcados de RAM y desensamblando el código de la cámara: 0x800529F8 / 0x80051FB4
// rotan la tabla, 0x800524D0 comprueba la colisión y 0x80051B10 suaviza. Tenchu 2, PAL España.
const GameCamera kGames[] = {
  {"SLES-02465", 0x81DE4, 0x82364, 0x8CB18, 0x8CB2C, 0x92580, 0x828D8, 0x828C8},
};

constexpr float kDeadZone = 0.22f;
constexpr float kYawSpeed = 2.6f;   // rad/s con el joystick a fondo
constexpr float kPitchSpeed = 1.2f; // rad/s
constexpr float kPitchMin = -0.35f, kPitchMax = 0.9f;
constexpr float kTargetJump = 2500.0f; // un salto así del objetivo = cinemática o cambio de escena
constexpr float kTau = 6.28318530718f;
// Distancia ante un muro: se acerca deprisa y se aleja despacio (fracción por segundo, aprox.).
constexpr float kPullIn = 14.0f, kPullOut = 2.5f, kHeightRate = 6.0f;

// Modos de cámara del juego que siguen al personaje: 0 quieto, 2 girando en el sitio, 3 andando o
// corriendo, 4 agachado. Con los demás (6 mirar con L1, 20 apuntar, nadar, cinemáticas…) manda el juego.
bool FollowMode(int16_t mode)
{
  return mode >= 0 && mode <= 4;
}

float Shape(float v)
{
  // Zona muerta y respuesta suave (cuadrática) para apuntar con precisión.
  const float a = std::fabs(v);
  if (a < kDeadZone)
    return 0.0f;
  const float t = (a - kDeadZone) / (1.0f - kDeadZone);
  return std::copysign(t * t, v);
}

int16_t Get16(const uint8_t* ram, uint32_t off)
{
  int16_t v;
  std::memcpy(&v, ram + off, 2);
  return v;
}

uint32_t Get32(const uint8_t* ram, uint32_t off)
{
  uint32_t v;
  std::memcpy(&v, ram + off, 4);
  return v;
}

// Una entrada de la tabla es un preajuste de cámara si sus 4 vectores tienen el relleno a cero, valores
// razonables y el de la cámara (el cuarto) no es diminuto (hay otros datos mezclados en la tabla).
// Sólo se giran los preajustes centrados detrás del personaje (X = 0); los laterales del juego (por
// ejemplo, pegado a una pared) se dejan como están.
bool IsPreset(const int16_t* e)
{
  if (e[0] != 0 || e[4] != 0 || e[8] != 0 || e[12] != 0)
    return false;
  for (int v = 0; v < 4; v++)
  {
    if (e[v * 4 + 3] != 0)
      return false;
    for (int c = 0; c < 3; c++)
      if (std::abs(e[v * 4 + c]) > 8000)
        return false;
  }
  return std::abs(e[12]) + std::abs(e[13]) + std::abs(e[14]) >= 500;
}

} // namespace

bool FreeCamera::SetGame(const std::string& serial)
{
  m_table_begin = m_table_end = 0;
  m_original.clear();
  m_modified = m_smooth_set = m_active = m_have_written = false;
  for (const GameCamera& g : kGames)
    if (serial == g.serial)
    {
      m_table_begin = g.table_begin;
      m_table_end = g.table_end;
      m_mode = g.mode;
      m_player = g.player;
      m_cam = g.cam;
      m_target = g.cam + 12;
      m_smooth = g.smooth;
      m_smooth_target = g.smooth_target;
    }
  return Available();
}

void FreeCamera::Restore(uint8_t* ram)
{
  if (m_modified && !m_original.empty())
    std::memcpy(ram + m_table_begin, m_original.data(), m_original.size() * 2);
  m_modified = false;
  if (m_smooth_set)
  {
    std::memcpy(ram + m_smooth, m_smooth_original, sizeof(m_smooth_original));
    std::memcpy(ram + m_smooth_target, m_smooth_target_original, sizeof(m_smooth_target_original));
  }
  m_smooth_set = false;
  m_have_written = false;
}

void FreeCamera::Apply(uint8_t* ram, float yaw, float pitch)
{
  // Giro respecto al personaje (alrededor de su eje vertical) y elevación (alrededor de su eje
  // lateral). En PS1 el eje Y apunta hacia abajo.
  const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
  std::vector<int16_t> t = m_original;
  for (size_t e = 0; e + 16 <= t.size(); e += 16)
  {
    if (!IsPreset(&m_original[e]))
      continue;
    // Sólo los vectores de la cámara (2 y 3; los del punto de mira, 0 y 1, no se giran), y siempre los
    // del preajuste de reposo (entrada 0): al correr el juego usa una cámara a 1000 unidades que sólo
    // queda lejos porque su suavizado va con retraso; sin ese retraso la distancia debe ser fija.
    for (int v = 2; v < 4; v++)
    {
      const float x = m_original[v * 4], y = m_original[v * 4 + 1], z = m_original[v * 4 + 2];
      const float y1 = y * cp - z * sp, z1 = y * sp + z * cp;
      const float x2 = x * cy + z1 * sy, z2 = -x * sy + z1 * cy;
      t[e + v * 4] = static_cast<int16_t>(std::lround(x2));
      t[e + v * 4 + 1] = static_cast<int16_t>(std::lround(y1));
      t[e + v * 4 + 2] = static_cast<int16_t>(std::lround(z2));
    }
  }
  std::memcpy(ram + m_table_begin, t.data(), t.size() * 2);
  m_modified = true;
}

void FreeCamera::Frame(uint8_t* ram, size_t size, float stick_x, float stick_y, bool reset, bool keep, float dt)
{
  if (!ram || !Available() || m_table_end > size || m_target + 12 > size || m_mode + 2 > size ||
      m_smooth + 12 > size || m_smooth_target + 12 > size)
    return;

  // Copia de la tabla original la primera vez (antes de tocarla).
  if (m_original.empty())
  {
    m_original.resize((m_table_end - m_table_begin) / 2);
    std::memcpy(m_original.data(), ram + m_table_begin, m_original.size() * 2);
    // Debe ser la tabla original: la primera entrada es un preajuste centrado (X = 0) que, girado,
    // tendría X (así no se toma como original una tabla que venga girada en un estado guardado).
    if (!IsPreset(m_original.data()))
    {
      m_original.clear(); // la tabla aún no está cargada (o no es la esperada)
      return;
    }
  }

  // Giro del personaje: puntero → +0x30 → rotación (giro en +2), en unidades de 4096 por vuelta.
  const uint32_t obj = Get32(ram, m_player) & 0x1FFFFF;
  const uint32_t rot = obj && obj + 0x34 <= size ? Get32(ram, obj + 0x30) & 0x1FFFFF : 0;
  const uint32_t posp = obj && obj + 0x30 <= size ? Get32(ram, obj + 0x2C) & 0x1FFFFF : 0;
  if (!rot || rot + 4 > size || !posp || posp + 12 > size)
  {
    Restore(ram);
    return;
  }
  int32_t player[3]; // posición del personaje (en los pies)
  std::memcpy(player, ram + posp, sizeof(player));
  const float player_yaw = (Get16(ram, rot + 2) & 0xFFF) * kTau / 4096.0f;

  int32_t target[3], cam[3];
  std::memcpy(target, ram + m_target, sizeof(target));
  std::memcpy(cam, ram + m_cam, sizeof(cam));

  // R3: la cámara gira hasta quedar detrás del personaje y después manda el juego.
  if (reset && m_active)
    m_returning = true;

  const float sx = Shape(stick_x), sy = Shape(stick_y);
  const bool follow = FollowMode(Get16(ram, m_mode)); // el juego usa una cámara que sigue al personaje
  if (!m_active)
  {
    if ((sx == 0 && sy == 0 && !keep) || !follow)
      return;
    // Se empieza desde donde está ahora la cámara del juego: sin saltos.
    const float dx = static_cast<float>(cam[0] - target[0]), dz = static_cast<float>(cam[2] - target[2]);
    m_dist = m_game_dist = std::sqrt(dx * dx + dz * dz);
    m_height = m_game_height = static_cast<float>(cam[1] - target[1]);
    if (m_dist < 300.0f)
      return;
    m_world_yaw = std::atan2(dx, dz);
    m_pitch = 0;
    m_returning = false;
    m_head = static_cast<float>(target[1] - player[1]); // altura del punto de mira (quieto)
    std::memcpy(m_last_target, player, sizeof(player));
    m_active = true;
  }

  // Si el personaje salta de sitio (cinemática, muerte, cambio de zona) manda de nuevo el juego.
  const float jx = static_cast<float>(player[0] - m_last_target[0]),
              jz = static_cast<float>(player[2] - m_last_target[2]);
  std::memcpy(m_last_target, player, sizeof(player));
  if (jx * jx + jz * jz > kTargetJump * kTargetJump)
  {
    m_active = false;
    Restore(ram);
    return;
  }

  // Apuntar, primera persona y demás modos: manda el juego (la cámara libre espera).
  if (!follow)
  {
    Restore(ram);
    return;
  }

  // Lo que quiere el juego (con colisión) en la dirección elegida: su suavizado, congelado, deja en
  // los parámetros la diferencia entre la cámara (la que escribimos) y su posición deseada. Se pone a
  // cero tras leerla, así sólo cuenta cuando el juego ha vuelto a calcularla (lo hace un frame sí y
  // otro no).
  if (m_smooth_set && m_have_written)
  {
    int16_t delta[3];
    std::memcpy(delta, ram + m_smooth + 6, sizeof(delta));
    if (delta[0] || delta[1] || delta[2])
    {
      const float gx = static_cast<float>(m_written[0] + delta[0] - m_written[3]),
                  gz = static_cast<float>(m_written[2] + delta[2] - m_written[5]);
      m_game_dist = std::sqrt(gx * gx + gz * gz);
      m_game_height = static_cast<float>(m_written[1] + delta[1] - m_written[4]);
      const int16_t zero[3] = {};
      std::memcpy(ram + m_smooth + 6, zero, sizeof(zero));
    }
  }

  if (m_returning && (sx != 0 || sy != 0))
    m_returning = false; // se vuelve a mover el joystick: se cancela la vuelta
  if (m_returning)
  {
    // Detrás del personaje la cámara queda en la dirección hacia la que él mira.
    const float k = 1.0f - std::exp(-9.0f * dt);
    const float diff = std::remainder(player_yaw - m_world_yaw, kTau);
    m_world_yaw += diff * k;
    m_pitch -= m_pitch * k;
    if (std::fabs(diff) < 0.04f && std::fabs(m_pitch) < 0.03f)
    {
      m_active = m_returning = false;
      Restore(ram);
      return;
    }
  }
  m_world_yaw += sx * kYawSpeed * m_speed * dt;
  // Como en los juegos actuales: joystick abajo = la cámara sube y mira hacia abajo.
  m_pitch = std::clamp(m_pitch + sy * kPitchSpeed * m_speed * dt, kPitchMin, kPitchMax);

  // El juego calcula su posición deseada girando su tabla (fija respecto al mundo: se descuenta
  // el giro del personaje) y con el suavizado congelado no mueve la cámara.
  Apply(ram, std::remainder(m_world_yaw - player_yaw, kTau), m_pitch);
  if (!m_smooth_set)
  {
    std::memcpy(m_smooth_original, ram + m_smooth, sizeof(m_smooth_original));
    std::memcpy(m_smooth_target_original, ram + m_smooth_target, sizeof(m_smooth_target_original));
    m_smooth_set = true;
  }
  const int16_t frozen[3] = {15, 0, 0}; // desplazamiento máximo: ni la cámara ni su punto de mira avanzan
  std::memcpy(ram + m_smooth, frozen, sizeof(frozen));
  std::memcpy(ram + m_smooth_target, frozen, sizeof(frozen));

  // Distancia y altura: hacia las del juego, deprisa al acercarse (muro) y despacio al alejarse.
  const float in = 1.0f - std::exp(-kPullIn * dt), out = 1.0f - std::exp(-kPullOut * dt);
  m_dist += (m_game_dist - m_dist) * (m_game_dist < m_dist ? in : out);
  m_height += (m_game_height - m_height) * (1.0f - std::exp(-kHeightRate * dt));

  // Órbita alrededor de la cabeza del personaje, mirándola (posición de la cámara y punto de mira).
  const int32_t head[3] = {player[0], player[1] + static_cast<int32_t>(std::lround(m_head)), player[2]};
  const int32_t pos[6] = {
    head[0] + static_cast<int32_t>(std::lround(m_dist * std::sin(m_world_yaw))),
    head[1] + static_cast<int32_t>(std::lround(m_height)),
    head[2] + static_cast<int32_t>(std::lround(m_dist * std::cos(m_world_yaw))),
    head[0],
    head[1],
    head[2],
  };
  if (std::getenv("TENSHU_DEBUG_CAMERA"))
    std::fprintf(stderr, "camara: juego %.0f/%.0f -> libre %.0f/%.0f\n", m_game_dist, m_game_height, m_dist,
                 m_height);
  std::memcpy(ram + m_cam, pos, sizeof(pos));
  std::memcpy(m_written, pos, sizeof(pos));
  m_have_written = true;
}

void FreeCamera::Steer(uint8_t* ram, size_t size, float move_x, float move_y, float dt)
{
  m_steerable = false;
  if (!ram || !Available() || !m_active || m_returning || m_mode + 2 > size || !FollowMode(Get16(ram, m_mode)))
    return;
  const uint32_t obj = Get32(ram, m_player) & 0x1FFFFF;
  const uint32_t rot = obj && obj + 0x34 <= size ? Get32(ram, obj + 0x30) & 0x1FFFFF : 0;
  if (!rot || rot + 4 > size)
    return;
  m_steerable = true;
  const float mag = std::sqrt(move_x * move_x + move_y * move_y);
  const float current = (Get16(ram, rot + 2) & 0xFFF) * kTau / 4096.0f;
  if (mag < 0.25f)
  {
    m_steer_yaw = current;
    return;
  }
  // Hacia dentro de la pantalla el personaje mira en la misma dirección que la cámara (que queda en
  // la dirección m_world_yaw desde su cabeza); la derecha de la pantalla resta.
  const float desired = m_world_yaw - std::atan2(move_x, -move_y);
  const float diff = std::remainder(desired - current, kTau);
  // Giro rápido pero suave (como en los juegos actuales); los giros grandes se notan menos bruscos.
  const float yaw = current + diff * (1.0f - std::exp(-16.0f * dt));
  const int16_t value = static_cast<int16_t>(static_cast<int>(std::lround(yaw / kTau * 4096.0f)) & 0xFFF);
  std::memcpy(ram + rot + 2, &value, 2);
  m_steer_yaw = yaw;
}
