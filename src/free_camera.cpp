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
  uint32_t mode;                   // modo de cámara (16 bits): 0 normal, 6 primera persona (L1), 20 apuntando
  uint32_t player;                 // puntero al personaje (+0x30 → su rotación; giro en +2)
  uint32_t target;                 // punto al que mira la cámara
};

// Localizadas con volcados de RAM y desensamblando el código de la cámara (0x800529F8 y
// 0x80051FB4, que rota estos vectores; 0x800524D0 comprueba la colisión). Tenchu 2, PAL España.
const GameCamera kGames[] = {
  {"SLES-02465", 0x81DE4, 0x82364, 0x8CB18, 0x8CB2C, 0x9258C},
};

constexpr float kDeadZone = 0.22f;
constexpr float kYawSpeed = 2.6f;   // rad/s con el joystick a fondo
constexpr float kPitchSpeed = 1.2f; // rad/s
constexpr float kPitchMin = -0.35f, kPitchMax = 0.9f;
constexpr float kTargetJump = 2500.0f; // un salto así del objetivo = cinemática o cambio de escena
constexpr float kTau = 6.28318530718f;

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
  m_modified = m_active = false;
  for (const GameCamera& g : kGames)
    if (serial == g.serial)
    {
      m_table_begin = g.table_begin;
      m_table_end = g.table_end;
      m_mode = g.mode;
      m_player = g.player;
      m_target = g.target;
    }
  return Available();
}

void FreeCamera::Restore(uint8_t* ram)
{
  if (m_modified && !m_original.empty())
    std::memcpy(ram + m_table_begin, m_original.data(), m_original.size() * 2);
  m_modified = false;
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
    for (int v = 0; v < 4; v++)
    {
      const float x = m_original[e + v * 4], y = m_original[e + v * 4 + 1], z = m_original[e + v * 4 + 2];
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

void FreeCamera::Frame(uint8_t* ram, size_t size, float stick_x, float stick_y, bool reset, float dt)
{
  if (!ram || !Available() || m_table_end > size || m_target + 12 > size || m_mode + 2 > size)
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
  if (!rot || rot + 4 > size)
  {
    Restore(ram);
    return;
  }
  const float player_yaw = (Get16(ram, rot + 2) & 0xFFF) * kTau / 4096.0f;

  int32_t target[3];
  std::memcpy(target, ram + m_target, sizeof(target));

  if (reset)
  {
    m_active = false;
    Restore(ram);
  }

  const float sx = Shape(stick_x), sy = Shape(stick_y);
  const bool normal = Get16(ram, m_mode) == 0; // el juego usa su cámara normal
  if (!m_active)
  {
    if ((sx == 0 && sy == 0) || !normal)
      return;
    // Se empieza detrás del personaje, donde ya está la cámara del juego: sin saltos.
    m_world_yaw = player_yaw;
    m_pitch = 0;
    std::memcpy(m_last_target, target, sizeof(target));
    m_active = true;
  }

  // Si el objetivo salta (cinemática, muerte, cambio de zona) se devuelve la cámara al juego.
  const float jx = static_cast<float>(target[0] - m_last_target[0]),
              jz = static_cast<float>(target[2] - m_last_target[2]);
  std::memcpy(m_last_target, target, sizeof(target));
  if (jx * jx + jz * jz > kTargetJump * kTargetJump)
  {
    m_active = false;
    Restore(ram);
    return;
  }

  // Apuntar, primera persona y demás modos usan la tabla original.
  if (!normal)
  {
    Restore(ram);
    return;
  }

  m_world_yaw += sx * kYawSpeed * m_speed * dt;
  // Como en los juegos actuales: joystick abajo = la cámara sube y mira hacia abajo.
  m_pitch = std::clamp(m_pitch + sy * kPitchSpeed * m_speed * dt, kPitchMin, kPitchMax);
  // La cámara queda fija respecto al mundo: se descuenta el giro del personaje.
  const float rel = std::remainder(m_world_yaw - player_yaw, kTau);
  if (std::getenv("TENSHU_DEBUG_CAMERA"))
    std::fprintf(stderr, "camara: personaje %.2f, mundo %.2f, relativo %.2f, elevación %.2f\n", player_yaw,
                 m_world_yaw, rel, m_pitch);
  Apply(ram, rel, m_pitch);
}
