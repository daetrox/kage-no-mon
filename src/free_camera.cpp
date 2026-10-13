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
  uint32_t cam, target;
};

// Localizadas volcando la RAM mientras el personaje gira (Tenchu 2, PAL España).
const GameCamera kGames[] = {
  {"SLES-02465", 0x92580, 0x9258C},
};

constexpr float kDeadZone = 0.22f;
constexpr float kYawSpeed = 2.6f;   // rad/s con el joystick a fondo
constexpr float kPitchSpeed = 1.4f; // rad/s
constexpr float kPitchMin = -0.12f, kPitchMax = 1.25f;
constexpr float kTargetJump = 2500.0f; // un salto así del objetivo = cinemática o cambio de escena

void Read3(const uint8_t* ram, uint32_t off, int32_t v[3])
{
  std::memcpy(v, ram + off, 12);
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

} // namespace

bool FreeCamera::SetGame(const std::string& serial)
{
  m_cam = m_target = 0;
  m_active = false;
  for (const GameCamera& g : kGames)
    if (serial == g.serial)
    {
      m_cam = g.cam;
      m_target = g.target;
    }
  return Available();
}

void FreeCamera::Frame(uint8_t* ram, size_t size, float stick_x, float stick_y, bool look, bool reset, float dt)
{
  if (!ram || !Available() || m_cam + 12 > size || m_target + 12 > size)
    return;
  if (reset)
    m_active = false;

  int32_t target[3], cam[3];
  Read3(ram, m_target, target);
  Read3(ram, m_cam, cam);

  const float sx = Shape(stick_x), sy = Shape(stick_y);
  if (!m_active)
  {
    if (sx == 0 && sy == 0)
      return;
    // Se empieza desde donde está ahora la cámara del juego, sin saltos.
    const float dx = static_cast<float>(cam[0] - target[0]), dy = static_cast<float>(cam[1] - target[1]),
                dz = static_cast<float>(cam[2] - target[2]);
    m_dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (m_dist < 500.0f || m_dist > 20000.0f)
      return; // vista en primera persona u otra situación rara: no se toca
    m_yaw = std::atan2(dx, dz);
    m_pitch = std::clamp(std::asin(std::clamp(-dy / m_dist, -1.0f, 1.0f)), kPitchMin, kPitchMax);
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
    return;
  }
  if (look)
    return; // L1: el juego pone la cámara en los ojos del personaje

  m_yaw += sx * kYawSpeed * dt;
  // Como en los juegos actuales: joystick abajo = la cámara sube y mira hacia abajo.
  m_pitch = std::clamp(m_pitch + sy * kPitchSpeed * dt, kPitchMin, kPitchMax);

  // En PS1 el eje Y apunta hacia abajo: la cámara sube restando.
  const float horiz = m_dist * std::cos(m_pitch);
  const int32_t out[3] = {
    target[0] + static_cast<int32_t>(std::lround(horiz * std::sin(m_yaw))),
    target[1] - static_cast<int32_t>(std::lround(m_dist * std::sin(m_pitch))),
    target[2] + static_cast<int32_t>(std::lround(horiz * std::cos(m_yaw))),
  };
  if (std::getenv("TENSHU_DEBUG_CAMERA"))
    std::fprintf(stderr, "camara: juego (%d,%d,%d) -> libre (%d,%d,%d)\n", cam[0], cam[1], cam[2], out[0], out[1],
                 out[2]);
  std::memcpy(ram + m_cam, out, sizeof(out));
}
