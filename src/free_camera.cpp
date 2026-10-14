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
  uint32_t char_list;              // punteros a los personajes del nivel (el primero, el jugador)
  uint32_t cam_matrix;             // matriz de cámara del GTE (0x80072100): 3x3 en 1/4096 y traslación en +0x14
};

// Localizadas con volcados de RAM y desensamblando el código de la cámara: 0x800529F8 / 0x80051FB4
// rotan la tabla, 0x800524D0 comprueba la colisión y 0x80051B10 suaviza. Tenchu 2, PAL España.
const GameCamera kGames[] = {
  {"SLES-02465", 0x81DE4, 0x82364, 0x8CB18, 0x8CB2C, 0x92580, 0x828D8, 0x828C8, 0xC0ED0, 0xCCE70},
};

constexpr float kDeadZone = 0.22f;
constexpr float kYawSpeed = 2.6f;   // rad/s con el joystick a fondo
constexpr float kPitchSpeed = 1.2f; // rad/s
constexpr float kPitchMin = -0.35f, kPitchMax = 0.9f;
constexpr float kTargetJump = 2500.0f; // un salto así del objetivo = cinemática o cambio de escena
constexpr float kTau = 6.28318530718f;
// El juego coloca la tabla sobre la posición del personaje elevada 0x73A (0x80051FB4).
constexpr float kBaseHeight = 0x73A;
// Distancia ante un muro: se acerca deprisa y se aleja despacio (fracción por segundo, aprox.).
constexpr float kPullIn = 14.0f, kPullOut = 2.5f, kHeightRate = 6.0f;
// Fijar enemigo: alcance al fijar y distancia a la que se suelta (unidades del juego; la cámara va
// a ~3000 del personaje), y cuánto se desplaza el punto de mira hacia el enemigo.
constexpr float kLockRange = 9000.0f, kLockLose = 14000.0f, kLockLookShift = 0.3f;
constexpr int kMaxCharacters = 64;
// Sólo enemigos a la vista: dentro de este ángulo respecto a la cámara (o muy cerca), y comprobados
// con la colisión del juego (como máximo kMaxProbes candidatos, kProbeFrames frames cada uno).
constexpr float kLockFov = 1.13f, kLockNear = 1500.0f;
constexpr size_t kMaxProbes = 6;
constexpr int kProbeFrames = 8, kProbeReads = 2;
constexpr float kProbeClear = 0.85f; // visible si el juego deja la cámara a ≥85 % de la distancia
// Proyección del juego: SetGeomScreen(300) en una pantalla de 320x256 (0x800170F0).
constexpr float kProjH = 300.0f, kScreenHalfW = 160.0f, kScreenHalfH = 128.0f;
// El centro de proyección queda 21 líneas por debajo del centro de la imagen visible (medido).
constexpr float kScreenOffsetY = 21.0f;
constexpr float kMarkAbove = 1650.0f; // coronilla del personaje (la marca se dibuja un poco más arriba)

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
      m_char_list = g.char_list;
      m_cam_matrix = g.cam_matrix;
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
    // Punto de mira (vectores 0 y 1): la cabeza del personaje, como el de la cámara libre. Así, en los
    // modos que reescriben el suavizado congelado (la guardia), el juego no lo arrastra hacia delante.
    for (int v = 0; v < 2; v++)
    {
      t[e + v * 4] = 0;
      t[e + v * 4 + 1] = static_cast<int16_t>(std::lround(m_head + kBaseHeight));
      t[e + v * 4 + 2] = 0;
    }
    // Cámara (vectores 2 y 3): siempre los del preajuste de reposo (entrada 0), girados. Al correr el
    // juego usa una cámara a 1000 unidades que sólo queda lejos porque su suavizado va con retraso;
    // sin ese retraso la distancia debe ser fija.
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

bool FreeCamera::EnemyPos(const uint8_t* ram, size_t size, uint32_t obj, int32_t pos[3]) const
{
  // Personaje: +0 clase (0 = jugador), +8 vida, +0x2C puntero a su posición.
  if (!obj || obj + 0x30 > size || Get16(ram, obj) == 0 || Get16(ram, obj + 8) <= 0)
    return false;
  const uint32_t p = Get32(ram, obj + 0x2C) & 0x1FFFFF;
  if (!p || p + 12 > size)
    return false;
  std::memcpy(pos, ram + p, 12);
  return true;
}

std::vector<uint32_t> FreeCamera::FindEnemies(const uint8_t* ram, size_t size, const int32_t player[3],
                                              float face_x, float face_z, int side, uint32_t exclude) const
{
  std::vector<std::pair<float, uint32_t>> found;
  if (!m_char_list || m_char_list + 4 * kMaxCharacters > size)
    return {};
  for (int i = 0; i < kMaxCharacters; i++)
  {
    const uint32_t ptr = Get32(ram, m_char_list + 4 * i);
    if (!ptr)
      break;
    const uint32_t obj = ptr & 0x1FFFFF;
    int32_t e[3];
    if (obj == exclude || (ptr & 0xFF000000) != 0x80000000 || !EnemyPos(ram, size, obj, e))
      continue;
    const float dx = static_cast<float>(e[0] - player[0]), dz = static_cast<float>(e[2] - player[2]);
    const float dist = std::sqrt(dx * dx + dz * dz);
    if (dist > kLockRange || dist < 1.0f)
      continue;
    // Ángulo respecto a hacia donde se mira (0..pi) y lado (producto vectorial).
    const float ang = std::acos(std::clamp((dx * face_x + dz * face_z) / dist, -1.0f, 1.0f));
    const float cross = face_x * dz - face_z * dx;
    if (side != 0 && (side > 0 ? cross > 0 : cross < 0))
      continue;
    if (side == 0 && ang > kLockFov && dist > kLockNear && !std::getenv("TENSHU_TEST_LOCK_ANYDIR"))
      continue; // fuera del campo de visión (salvo si está encima)
    found.push_back({dist * (1.0f + 1.5f * ang / 3.14159265f), obj});
  }
  std::sort(found.begin(), found.end());
  std::vector<uint32_t> out;
  for (const auto& f : found)
    out.push_back(f.second);
  if (out.size() > kMaxProbes)
    out.resize(kMaxProbes);
  return out;
}

void FreeCamera::StartProbe(std::vector<uint32_t> candidates)
{
  m_probe = std::move(candidates);
  m_probe_frames = m_probe_reads = 0;
  m_probe_min = 1e9f;
  if (m_probe.empty())
    m_lock_event = LockEvent::NoTarget;
}

void FreeCamera::ApplyProbe(uint8_t* ram, float rel_yaw, float dist, float height)
{
  // Los vectores de la cámara de todos los preajustes apuntan al candidato (mismo convenio que
  // Apply: girados `rel_yaw` respecto al personaje); el juego calcula hasta dónde puede llegar.
  std::vector<int16_t> t = m_original;
  const float sy = std::sin(rel_yaw), cy = std::cos(rel_yaw);
  const float d = std::min(dist, 30000.0f);
  for (size_t e = 0; e + 16 <= t.size(); e += 16)
  {
    if (!IsPreset(&m_original[e]))
      continue;
    for (int v = 0; v < 2; v++)
    {
      t[e + v * 4] = 0;
      t[e + v * 4 + 1] = static_cast<int16_t>(std::lround(m_head + kBaseHeight));
      t[e + v * 4 + 2] = 0;
    }
    for (int v = 2; v < 4; v++)
    {
      const float len = v == 3 ? d : d * 0.25f;
      t[e + v * 4] = static_cast<int16_t>(std::lround(len * sy));
      t[e + v * 4 + 1] = static_cast<int16_t>(std::clamp(height, -30000.0f, 30000.0f));
      t[e + v * 4 + 2] = static_cast<int16_t>(std::lround(len * cy));
    }
  }
  std::memcpy(ram + m_table_begin, t.data(), t.size() * 2);
  m_modified = true;
}

bool FreeCamera::LockMarker(const uint8_t* ram, size_t size, float* sx, float* sy, bool debug_player) const
{
  int32_t p[3];
  if (debug_player)
  {
    const uint32_t obj = Get32(ram, m_player) & 0x1FFFFF;
    const uint32_t posp = obj && obj + 0x30 <= size ? Get32(ram, obj + 0x2C) & 0x1FFFFF : 0;
    if (!posp || posp + 12 > size)
      return false;
    std::memcpy(p, ram + posp, 12);
  }
  else if (!m_lock_obj || !EnemyPos(ram, size, m_lock_obj, p))
  {
    return false;
  }
  // Se proyecta con la matriz de cámara que el propio juego carga en el GTE (rotación, escala de
  // aspecto y del parche 16:9 incluidas): v_cám = M·v/4096 + T; pantalla = H·v/z + centro.
  if (!m_cam_matrix || m_cam_matrix + 0x20 > size)
    return false;
  int16_t m[9];
  int32_t t[3];
  std::memcpy(m, ram + m_cam_matrix, sizeof(m));
  std::memcpy(t, ram + m_cam_matrix + 0x14, sizeof(t));
  const float v[3] = {static_cast<float>(p[0]), static_cast<float>(p[1]) - kMarkAbove, static_cast<float>(p[2])};
  float c[3];
  for (int i = 0; i < 3; i++)
    c[i] = (m[i * 3] * v[0] + m[i * 3 + 1] * v[1] + m[i * 3 + 2] * v[2]) / 4096.0f + t[i];
  if (c[2] < 200.0f)
    return false; // detrás de la cámara o pegado a ella
  *sx = kProjH * c[0] / c[2] / kScreenHalfW;
  *sy = (kProjH * c[1] / c[2] + kScreenOffsetY) / kScreenHalfH;
  if (std::getenv("TENSHU_DEBUG_MARK"))
    std::fprintf(stderr, "marca: c=(%.0f %.0f %.0f) -> %.2f %.2f\n", c[0], c[1], c[2], *sx, *sy);
  return std::fabs(*sx) < 1.2f && std::fabs(*sy) < 1.2f;
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
  if (reset && m_lock_obj)
  {
    m_lock_obj = 0;
    m_lock_event = LockEvent::Released;
  }
  if (reset && m_active)
    m_returning = true;

  float sx = Shape(stick_x);
  const float sy = Shape(stick_y);
  const bool follow = FollowMode(Get16(ram, m_mode)); // el juego usa una cámara que sigue al personaje

  // L3: fijar o soltar enemigo. Se elige hacia donde mira la cámara (o el personaje, si la cámara es
  // la del juego); con un ángulo `a`, la vista va en la dirección -(sin a, cos a).
  if (m_lock_request)
  {
    m_lock_request = false;
    if (m_lock_obj)
    {
      m_lock_obj = 0;
      m_lock_event = LockEvent::Released;
    }
    else if (follow)
    {
      const float look = m_active ? m_world_yaw : player_yaw;
      StartProbe(FindEnemies(ram, size, player, -std::sin(look), -std::cos(look), 0, 0));
      m_returning = false;
      m_flick_ready = false;
    }
  }
  int32_t enemy[3] = {};
  if (m_lock_obj)
  {
    const bool alive = EnemyPos(ram, size, m_lock_obj, enemy);
    const float ex = static_cast<float>(enemy[0] - player[0]), ez = static_cast<float>(enemy[2] - player[2]);
    if (!alive || ex * ex + ez * ez > kLockLose * kLockLose)
    {
      m_lock_obj = 0;
      m_lock_event = LockEvent::Lost;
    }
    else
    {
      // Joystick derecho a un lado (desde el centro): cambia al enemigo de ese lado.
      if (std::fabs(stick_x) < 0.3f)
        m_flick_ready = true;
      else if (m_flick_ready && std::fabs(stick_x) > 0.7f)
      {
        m_flick_ready = false;
        const float len = std::max(1.0f, std::sqrt(ex * ex + ez * ez));
        // Se comprueba que se vea; mientras, sigue fijado el actual.
        StartProbe(FindEnemies(ram, size, player, ex / len, ez / len, stick_x > 0 ? 1 : -1, m_lock_obj));
      }
      sx = 0; // el giro manual queda en manos del fijado
      // El personaje mira al enemigo y la cámara queda detrás de él, en el mismo ángulo.
      m_lock_yaw = std::atan2(static_cast<float>(player[0] - enemy[0]), static_cast<float>(player[2] - enemy[2]));
    }
  }

  if (!m_active)
  {
    if ((sx == 0 && sy == 0 && !keep && !m_lock_obj && m_probe.empty()) || !follow)
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
  // Al cambiar de modo de cámara (guardia → correr…) y al desenvainar o envainar (planos especiales
  // del juego) la posición deseada da saltos que no son muros: durante un momento no se tiene en cuenta.
  const int16_t mode = Get16(ram, m_mode);
  const uint16_t action = obj + 0x7A <= size ? static_cast<uint16_t>(Get16(ram, obj + 0x78)) : 0;
  if (mode != m_last_mode || (action & 0xFF00) == 0x700)
    m_ignore_game = 0.3f;
  m_last_mode = mode;
  m_ignore_game = std::max(0.0f, m_ignore_game - dt);
  // Sondeo de visibilidad en curso: la distancia que deja el juego es la del candidato, no la de la
  // cámara (que mientras tanto se queda donde está).
  bool probing = false;
  if (!m_probe.empty())
  {
    int32_t e[3];
    if (!EnemyPos(ram, size, m_probe.front(), e))
    {
      m_probe.erase(m_probe.begin());
      m_probe_frames = m_probe_reads = 0;
      m_probe_min = 1e9f;
    }
    else
    {
      probing = true;
      const float dx = static_cast<float>(e[0] - player[0]), dz = static_cast<float>(e[2] - player[2]);
      m_probe_dist = std::sqrt(dx * dx + dz * dz);
      if (m_smooth_set && m_have_written && m_probe_frames > 0)
      {
        int16_t delta[3];
        std::memcpy(delta, ram + m_smooth + 6, sizeof(delta));
        if (delta[0] || delta[1] || delta[2])
        {
          const float gx = static_cast<float>(m_written[0] + delta[0] - m_written[3]),
                      gz = static_cast<float>(m_written[2] + delta[2] - m_written[5]);
          m_probe_min = std::min(m_probe_min, std::sqrt(gx * gx + gz * gz));
          m_probe_reads++;
          const int16_t zero[3] = {};
          std::memcpy(ram + m_smooth + 6, zero, sizeof(zero));
        }
      }
      if (std::getenv("TENSHU_DEBUG_CAMERA"))
        std::fprintf(stderr, "sondeo: objetivo %.0f, juego %.0f (%d lecturas)\n", m_probe_dist, m_probe_min,
                     m_probe_reads);
      if (m_probe_reads >= kProbeReads || m_probe_frames >= kProbeFrames)
      {
        if (m_probe_reads >= kProbeReads && m_probe_min >= kProbeClear * m_probe_dist)
        {
          m_lock_obj = m_probe.front(); // a la vista
          m_lock_event = LockEvent::Locked;
          m_probe.clear();
          probing = false;
        }
        else
        {
          m_probe.erase(m_probe.begin());
          if (m_probe.empty() && !m_lock_obj)
            m_lock_event = LockEvent::NoTarget;
        }
        m_probe_frames = m_probe_reads = 0;
        m_probe_min = 1e9f;
        if (m_lock_obj && EnemyPos(ram, size, m_lock_obj, enemy))
          m_lock_yaw =
            std::atan2(static_cast<float>(player[0] - enemy[0]), static_cast<float>(player[2] - enemy[2]));
      }
      else
      {
        m_probe_frames++;
      }
    }
  }
  if (!probing && m_smooth_set && m_have_written)
  {
    int16_t delta[3];
    std::memcpy(delta, ram + m_smooth + 6, sizeof(delta));
    if ((delta[0] || delta[1] || delta[2]) && m_ignore_game > 0)
    {
      const int16_t zero[3] = {};
      std::memcpy(ram + m_smooth + 6, zero, sizeof(zero));
    }
    else if (delta[0] || delta[1] || delta[2])
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
  if (m_lock_obj)
    m_world_yaw += std::remainder(m_lock_yaw - m_world_yaw, kTau) * (1.0f - std::exp(-7.0f * dt));
  m_world_yaw += sx * kYawSpeed * m_speed * dt;
  // Como en los juegos actuales: joystick abajo = la cámara sube y mira hacia abajo.
  m_pitch = std::clamp(m_pitch + sy * kPitchSpeed * m_speed * dt, kPitchMin, kPitchMax);

  // El juego calcula su posición deseada girando su tabla (fija respecto al mundo: se descuenta
  // el giro del personaje) y con el suavizado congelado no mueve la cámara.
  if (!m_probe.empty())
  {
    int32_t e[3];
    if (EnemyPos(ram, size, m_probe.front(), e))
    {
      const float ang = std::atan2(static_cast<float>(e[0] - player[0]), static_cast<float>(e[2] - player[2]));
      // Altura en la tabla: relativa a los pies del personaje elevados kBaseHeight; al nivel de la
      // cabeza del candidato.
      ApplyProbe(ram, std::remainder(ang - player_yaw, kTau), m_probe_dist,
                 static_cast<float>(e[1] - player[1]) + m_head + kBaseHeight);
    }
  }
  else
  {
    Apply(ram, std::remainder(m_world_yaw - player_yaw, kTau), m_pitch);
  }
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
  int32_t pos[6] = {
    head[0] + static_cast<int32_t>(std::lround(m_dist * std::sin(m_world_yaw))),
    head[1] + static_cast<int32_t>(std::lround(m_height)),
    head[2] + static_cast<int32_t>(std::lround(m_dist * std::cos(m_world_yaw))),
    head[0],
    head[1],
    head[2],
  };
  // Con un enemigo fijado, el punto de mira se adelanta hacia él para encuadrar a los dos.
  if (m_lock_obj)
  {
    pos[3] += static_cast<int32_t>(std::lround((enemy[0] - player[0]) * kLockLookShift));
    pos[5] += static_cast<int32_t>(std::lround((enemy[2] - player[2]) * kLockLookShift));
  }
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
  if (mag < 0.25f && m_lock_obj)
  {
    // Quieto con un enemigo fijado: el personaje se gira hacia él.
    const float yaw = current + std::remainder(m_lock_yaw - current, kTau) * (1.0f - std::exp(-12.0f * dt));
    const int16_t value = static_cast<int16_t>(static_cast<int>(std::lround(yaw / kTau * 4096.0f)) & 0xFFF);
    std::memcpy(ram + rot + 2, &value, 2);
    m_steer_yaw = yaw;
    return;
  }
  if (mag < 0.25f)
  {
    m_steer_yaw = current;
    return;
  }
  // Hacia dentro de la pantalla el personaje mira en la misma dirección que la cámara (que queda en
  // la dirección m_world_yaw desde su cabeza); la derecha de la pantalla suma.
  const float desired = m_world_yaw + std::atan2(move_x, -move_y);
  const float diff = std::remainder(desired - current, kTau);
  // Giro rápido pero suave (como en los juegos actuales); los giros grandes se notan menos bruscos.
  const float yaw = current + diff * (1.0f - std::exp(-16.0f * dt));
  const int16_t value = static_cast<int16_t>(static_cast<int>(std::lround(yaw / kTau * 4096.0f)) & 0xFFF);
  std::memcpy(ram + rot + 2, &value, 2);
  m_steer_yaw = yaw;
}

std::string FreeCamera::BlockPatch(const std::string& serial, bool modern)
{
  // Función del daño (0x80020A..): para el personaje golpeado, si su acción es 0x104 (guardia: andar
  // hacia atrás con el arma) el golpe se bloquea. Para el jugador hay además una rama propia
  // (0x80020B74-0x80020BB4) que, con el arma fuera, bloquea al azar 1 de cada 3 golpes según un valor
  // del personaje (+0x12). Con el movimiento 360° nunca se anda hacia atrás, así que esa rama se
  // reescribe para que el jugador bloquee siempre que esté de pie con el arma fuera (acción 0x100,
  // su pose normal):
  //   0x80020B74  lh   v0, 0x12(a0)  →  lh   v0, 0x78(a0)      acción del jugador
  //   0x80020B7C  slti v0, v0, 5     →  xori v0, v0, 0x100     0 si está de pie con el arma fuera
  //   0x80020B80  bnez v0, no-bloquea   (sin cambios)
  //   0x80020B88  jal  0x8007DB50    →  j    0x80020BB8        bloquea
  // Los enemigos no pasan por esa rama. Se escribe como códigos GameShark (el núcleo invalida el código
  // recompilado), cada uno condicionado a la instrucción «lui a0, 0x8009» de 0x80020B1C. Con los
  // controles de tanque se escriben los valores originales, por si un estado guardado los trae
  // cambiados (también 0x80020B00, que una versión de prueba cambiaba).
  if (serial != "SLES-02465")
    return "";
  static const struct
  {
    uint32_t addr;
    uint16_t original, patched;
  } kWrites[] = {
    {0x80020B74, 0x0012, 0x0078}, {0x80020B7C, 0x0005, 0x0100}, {0x80020B7E, 0x2842, 0x3842},
    {0x80020B88, 0xF6D4, 0x82EE}, {0x80020B8A, 0x0C01, 0x0800}, {0x80020B00, 0x0104, 0x0104},
  };
  std::string code;
  char line[64];
  for (const auto& w : kWrites)
  {
    std::snprintf(line, sizeof(line), "%sD0020B1E 3C04+%08X %04X", code.empty() ? "" : "+",
                  0x80000000u | (w.addr & 0xFFFFFF), modern ? w.patched : w.original);
    code += line;
  }
  return code;
}
