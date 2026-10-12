#include "achievements.h"
#include "config.h"
#include "json.h"

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <thread>

extern "C" {
#include "rc_consoles.h"
#include "rc_hash.h"
#include "rc_runtime.h"
}

namespace {

constexpr const char* kUserAgent = "Tenshu2Launcher/0.2 (Linux) rcheevos/12.5";
constexpr const char* kServer = "https://retroachievements.org/dorequest.php";

Achievements* g_active = nullptr; // sesión con el runtime activo (callbacks sin userdata)

std::string ReadAll(const std::string& path)
{
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string UrlEncode(const std::string& s)
{
  std::string out;
  char buf[4];
  for (unsigned char c : s)
  {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out += static_cast<char>(c);
    else
    {
      std::snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

// Ejecuta curl pasando URL y datos por la entrada estándar (-K -), para que credenciales y
// tokens nunca aparezcan en la lista de procesos. Devuelve true si la descarga tuvo éxito.
bool CurlFetch(const std::string& url, const std::string& post_data, const std::string& out_path)
{
  std::string tmp = out_path + ".part";
  const std::string cmd = "curl -sSfL --max-time 60 -K - -o '" + tmp + "' 2>/dev/null";
  FILE* p = popen(cmd.c_str(), "w");
  if (!p)
    return false;
  auto esc = [](const std::string& v) {
    std::string r;
    for (char c : v)
    {
      if (c == '"' || c == '\\')
        r += '\\';
      r += c;
    }
    return r;
  };
  std::fprintf(p, "url = \"%s\"\n", esc(url).c_str());
  std::fprintf(p, "user-agent = \"%s\"\n", kUserAgent);
  if (!post_data.empty())
    std::fprintf(p, "data = \"%s\"\n", esc(post_data).c_str());
  const int status = pclose(p);
  if (status != 0)
  {
    std::remove(tmp.c_str());
    return false;
  }
  return std::rename(tmp.c_str(), out_path.c_str()) == 0;
}

uint32_t ReadMemory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, void* ud)
{
  auto* self = static_cast<std::pair<const uint8_t*, size_t>*>(ud);
  // Mapa de PS1 en RA: 0x000000-0x1FFFFF = RAM principal (2 MB).
  if (!self->first || address >= self->second)
    return 0;
  const uint32_t n = static_cast<uint32_t>(std::min<size_t>(num_bytes, self->second - address));
  std::memcpy(buffer, self->first + address, n);
  return n;
}

} // namespace

std::string Achievements::GameDir() const
{
  return Paths::DataDir() + "/achievements/" + std::to_string(m_game_id);
}

void Achievements::SetGame(const std::string& game_path)
{
  if (game_path == m_game_path && m_state != State::NoGame)
    return;
  m_game_path = game_path;
  m_list.clear();
  m_game_id = 0;
  m_game_title.clear();
  m_state = State::NoGame;
  if (!Paths::FileExists(game_path))
    return;

  // Hash RA del disco (sólo lee SYSTEM.CNF y el ejecutable: es rápido).
  char hash[33] = {0};
  rc_hash_iterator_t it;
  rc_hash_initialize_iterator(&it, game_path.c_str(), nullptr, 0);
  const int ok = rc_hash_generate(hash, RC_CONSOLE_PLAYSTATION, &it);
  rc_hash_destroy_iterator(&it);
  if (!ok)
  {
    m_state = State::NotSupported;
    return;
  }
  m_hash = hash;

  // Caché hash -> id de juego.
  std::ifstream idx(Paths::DataDir() + "/achievements/hashes.txt");
  std::string h;
  uint32_t id;
  while (idx >> h >> id)
    if (h == m_hash)
      m_game_id = id;

  m_state = (m_game_id && LoadCache()) ? State::Ready : State::NeedLogin;
}

bool Achievements::LoadCache()
{
  JsonValue root;
  if (!JsonParser::Parse(ReadAll(GameDir() + "/set.json"), &root))
    return false;
  const JsonValue& patch = root["PatchData"];
  if (patch.IsNull())
    return false;
  m_game_title = patch["Title"].AsString();
  m_list.clear();
  for (const JsonValue& a : patch["Achievements"].arr)
  {
    // Flags 3 = oficiales; 5 = no oficiales (se ignoran).
    if (static_cast<int>(a["Flags"].AsNumber()) != 3)
      continue;
    // IDs >= 101000000 no son del juego: el servidor los inyecta como avisos para clientes que
    // no reconoce (p. ej. "Warning: Unknown Emulator"). RetroArch y DuckStation también los omiten.
    if (a["ID"].AsNumber() >= 101000000.0)
      continue;
    Achievement ach;
    ach.id = static_cast<uint32_t>(a["ID"].AsNumber());
    ach.title = a["Title"].AsString();
    ach.description = a["Description"].AsString();
    ach.badge = a["BadgeName"].AsString();
    ach.memaddr = a["MemAddr"].AsString();
    ach.author = a["Author"].AsString();
    ach.points = static_cast<int>(a["Points"].AsNumber());
    m_list.push_back(std::move(ach));
  }
  LoadUnlocks();
  return !m_list.empty();
}

void Achievements::LoadUnlocks()
{
  std::ifstream in(GameDir() + "/unlocks.txt");
  uint32_t id;
  int64_t when;
  bool stale = false;
  while (in >> id >> when)
  {
    if (Achievement* a = Find(id))
    {
      a->unlocked = true;
      a->unlock_time = when;
    }
    else
    {
      stale = true; // p. ej. el aviso de emulador desbloqueado por versiones anteriores
    }
  }
  in.close();
  if (stale)
    SaveUnlocks();
}

void Achievements::SaveUnlocks() const
{
  std::ofstream out(GameDir() + "/unlocks.txt", std::ios::trunc);
  for (const Achievement& a : m_list)
    if (a.unlocked)
      out << a.id << " " << a.unlock_time << "\n";
}

void Achievements::ResetUnlocks()
{
  for (Achievement& a : m_list)
  {
    a.unlocked = false;
    a.unlock_time = 0;
  }
  SaveUnlocks();
}

std::vector<std::string> Achievements::Authors() const
{
  std::vector<std::pair<std::string, int>> count;
  for (const Achievement& a : m_list)
  {
    if (a.author.empty())
      continue;
    auto it = std::find_if(count.begin(), count.end(), [&](const auto& c) { return c.first == a.author; });
    if (it == count.end())
      count.emplace_back(a.author, 1);
    else
      it->second++;
  }
  std::stable_sort(count.begin(), count.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
  std::vector<std::string> out;
  for (const auto& c : count)
    out.push_back(c.first);
  return out;
}

Achievement* Achievements::Find(uint32_t id)
{
  for (Achievement& a : m_list)
    if (a.id == id)
      return &a;
  return nullptr;
}

int Achievements::EarnedCount() const
{
  int n = 0;
  for (const Achievement& a : m_list)
    n += a.unlocked;
  return n;
}

int Achievements::EarnedPoints() const
{
  int n = 0;
  for (const Achievement& a : m_list)
    if (a.unlocked)
      n += a.points;
  return n;
}

int Achievements::TotalPoints() const
{
  int n = 0;
  for (const Achievement& a : m_list)
    n += a.points;
  return n;
}

void Achievements::LoadIcon(Achievement& a, Renderer& r)
{
  if (a.icon_tried)
    return;
  a.icon_tried = true;
  const std::string path = GameDir() + "/badges/" + a.badge + ".png";
  if (Paths::FileExists(path))
    r.LoadTexture(path, &a.icon, false);
}

std::string Achievements::Status() const
{
  switch (m_state)
  {
    case State::NoGame: return "Selecciona primero la imagen del juego en OPCIONES.";
    case State::NotSupported: return "Este disco no tiene logros en RetroAchievements.";
    case State::NeedLogin: return "Inicia sesión una vez para descargar los logros. Después funcionan sin conexión.";
    case State::Working:
    {
      std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
      return m_progress;
    }
    case State::Ready: return {};
  }
  return {};
}

// --- Descarga ----------------------------------------------------------------------------------

void Achievements::StartDownload(const std::string& user, const std::string& password)
{
  if (m_thread_running || m_hash.empty())
    return;
  m_state = State::Working;
  m_thread_running = true;
  m_thread_done = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_progress = "Conectando con RetroAchievements…";
    m_error.clear();
  }
  std::thread(&Achievements::DownloadThread, this, user, password).detach();
}

void Achievements::DownloadThread(std::string user, std::string password)
{
  auto progress = [this](const std::string& s) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_progress = s;
  };
  auto fail = [this](const std::string& s) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_error = s;
    m_thread_done = true;
  };

  const std::string base = Paths::DataDir() + "/achievements";
  Paths::MakeDirs(base);
  const std::string tmp = base + "/response.json";
  JsonValue root;

  // 1) Identificar el juego por el hash del disco.
  uint32_t game_id = m_game_id;
  if (!game_id)
  {
    progress("Identificando el disco…");
    if (!CurlFetch(kServer, "r=gameid&m=" + m_hash, tmp) || !JsonParser::Parse(ReadAll(tmp), &root))
      return fail("No se pudo conectar con RetroAchievements.\nComprueba tu conexión a internet.");
    game_id = static_cast<uint32_t>(root["GameID"].AsNumber());
    if (!game_id)
      return fail("RetroAchievements no reconoce esta versión del disco.");
  }

  // 2) Iniciar sesión (la contraseña sólo viaja en esta petición y no se guarda).
  progress("Iniciando sesión…");
  root = {};
  if (!CurlFetch(kServer, "r=login2&u=" + UrlEncode(user) + "&p=" + UrlEncode(password), tmp) ||
      !JsonParser::Parse(ReadAll(tmp), &root))
    return fail("No se pudo iniciar sesión (sin respuesta del servidor).");
  std::remove(tmp.c_str());
  if (root["Success"].type != JsonValue::Type::Bool || !root["Success"].b)
  {
    const std::string err = root["Error"].AsString();
    return fail("Inicio de sesión rechazado" + (err.empty() ? std::string(".") : ":\n" + err));
  }
  const std::string token = root["Token"].AsString();
  const std::string real_user = root["User"].AsString().empty() ? user : root["User"].AsString();

  // 3) Descargar el set de logros.
  progress("Descargando logros…");
  const std::string dir = base + "/" + std::to_string(game_id);
  Paths::MakeDirs(dir + "/badges");
  const std::string set_path = dir + "/set.json";
  if (!CurlFetch(kServer, "r=patch&u=" + UrlEncode(real_user) + "&t=" + UrlEncode(token) + "&g=" + std::to_string(game_id),
                 set_path))
    return fail("No se pudo descargar el set de logros.");
  root = {};
  if (!JsonParser::Parse(ReadAll(set_path), &root) || root["PatchData"].IsNull())
    return fail("Respuesta inesperada del servidor al descargar los logros.");

  // 4) Insignias.
  const JsonValue& list = root["PatchData"]["Achievements"];
  int done = 0;
  for (const JsonValue& a : list.arr)
  {
    const std::string badge = a["BadgeName"].AsString();
    progress("Descargando insignias… " + std::to_string(++done) + "/" + std::to_string(list.arr.size()));
    const std::string out = dir + "/badges/" + badge + ".png";
    if (!badge.empty() && !Paths::FileExists(out))
      CurlFetch("https://media.retroachievements.org/Badge/" + badge + ".png", "", out);
  }

  // Guardar hash -> id.
  {
    std::ofstream idx(base + "/hashes.txt", std::ios::app);
    idx << m_hash << " " << game_id << "\n";
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_game_id = game_id;
  }
  m_thread_done = true;
}

void Achievements::Poll()
{
  if (!m_thread_running || !m_thread_done)
    return;
  m_thread_running = false;
  std::string err;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    err = m_error;
  }
  if (!err.empty())
  {
    m_state = State::NeedLogin;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_progress = err;
    return;
  }
  m_state = LoadCache() ? State::Ready : State::NeedLogin;
}

// --- Ejecución ---------------------------------------------------------------------------------

void Achievements::BeginSession()
{
  EndSession();
  if (m_state != State::Ready)
    return;
  auto* rt = rc_runtime_alloc();
  int active = 0;
  for (const Achievement& a : m_list)
  {
    if (a.unlocked)
      continue;
    if (rc_runtime_activate_achievement(rt, a.id, a.memaddr.c_str(), nullptr, 0) == RC_OK)
      active++;
    else
      SDL_Log("Logro %u: lógica no válida", a.id);
  }
  m_runtime = rt;
  g_active = this;
  SDL_Log("Logros activos: %d de %zu", active, m_list.size());
}

void Achievements::EndSession()
{
  if (m_runtime)
  {
    rc_runtime_destroy(static_cast<rc_runtime_t*>(m_runtime));
    m_runtime = nullptr;
  }
  if (g_active == this)
    g_active = nullptr;
}

void Achievements::DoFrame(const uint8_t* ram, size_t ram_size)
{
  if (!m_runtime || !ram)
    return;
  std::pair<const uint8_t*, size_t> mem(ram, ram_size);
  rc_runtime_do_frame(
    static_cast<rc_runtime_t*>(m_runtime),
    [](const rc_runtime_event_t* ev) {
      if (!g_active)
        return;
      g_active->LogEvent(ev->type, ev->id);
      if (ev->type == RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED)
        g_active->Unlock(ev->id);
    },
    &ReadMemory, &mem, nullptr);
  // Desactivar fuera del callback los logros que acaban de cumplirse.
  for (uint32_t id : m_pending_deactivate)
    rc_runtime_deactivate_achievement(static_cast<rc_runtime_t*>(m_runtime), id);
  m_pending_deactivate.clear();
}

// Registro de diagnóstico: cambios de estado de los logros durante la partida.
void Achievements::LogEvent(int type, uint32_t id)
{
  const char* name = nullptr;
  switch (type)
  {
    case RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED: name = "PREPARADO"; break;
    case RC_RUNTIME_EVENT_ACHIEVEMENT_UNPRIMED: name = "YA NO PREPARADO"; break;
    case RC_RUNTIME_EVENT_ACHIEVEMENT_RESET: name = "REINICIADO"; break;
    case RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED: name = "DESBLOQUEADO"; break;
    case RC_RUNTIME_EVENT_ACHIEVEMENT_DISABLED: name = "DESACTIVADO (dirección no válida)"; break;
    default: return;
  }
  const Achievement* a = Find(id);
  std::ofstream log(GameDir() + "/eventos.log", std::ios::app);
  char when[32];
  const time_t t = std::time(nullptr);
  std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
  log << when << "  " << name << "  " << id << "  " << (a ? a->title : "?") << "\n";
}

void Achievements::Unlock(uint32_t id)
{
  Achievement* a = Find(id);
  if (!a || a->unlocked)
    return;
  a->unlocked = true;
  a->unlock_time = static_cast<int64_t>(std::time(nullptr));
  SaveUnlocks();
  m_pending_deactivate.push_back(id);
  m_new_unlocks.push_back(id);
}

std::vector<uint32_t> Achievements::TakeNewUnlocks()
{
  std::vector<uint32_t> out;
  out.swap(m_new_unlocks);
  return out;
}
