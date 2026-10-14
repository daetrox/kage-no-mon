// Logros de RetroAchievements que funcionan sin conexión.
//
// Una sola vez (con internet) se inicia sesión y se descarga el set del juego y sus insignias.
// Después todo es local: rcheevos evalúa la lógica de cada logro leyendo la RAM emulada en
// cada frame, y los desbloqueos se guardan en disco (no se envían a RetroAchievements).
#pragma once
#include "i18n.h"
#include "renderer.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct Achievement
{
  uint32_t id = 0;
  std::string title;       // traducido al español
  std::string description;
  std::string title_en;    // original de RetroAchievements (inglés)
  std::string description_en;
  const std::string& Title() const { return LangEn() ? title_en : title; }
  const std::string& Description() const { return LangEn() ? description_en : description; }
  std::string badge;   // nombre de la insignia en RA
  std::string memaddr; // lógica del logro (formato rcheevos)
  std::string author;  // autor del logro en RetroAchievements
  int points = 0;
  bool unlocked = false;
  bool hardcore = false;   // desbloqueado en modo hardcore
  int64_t unlock_time = 0; // epoch
  Texture icon;            // insignia cargada bajo demanda
  bool icon_tried = false;
};

class Achievements
{
public:
  enum class State
  {
    NoGame,      // aún no hay disco
    NeedLogin,   // no hay set descargado
    Working,     // descargando
    Ready,       // set en caché, listo offline
    NotSupported // el disco no tiene logros en RA
  };

  // Identifica el disco (hash RA calculado en local) y carga la caché si existe.
  void SetGame(const std::string& game_path);
  State GetState() const { return m_state; }
  std::string Status() const;
  const std::string& GameTitle() const { return m_game_title; }

  // Descarga (hilo aparte): inicia sesión, baja el set y las insignias. La contraseña no se guarda.
  void StartDownload(const std::string& user, const std::string& password);
  void Poll(); // llamar cada frame en el hilo principal

  std::vector<Achievement>& List() { return m_list; }
  int EarnedCount() const;
  int HardcoreCount() const;
  int EarnedPoints() const;
  int TotalPoints() const;
  void LoadIcon(Achievement& a, Renderer& r);
  void ResetUnlocks();
  // Autores del set (de más a menos logros creados), para los créditos.
  std::vector<std::string> Authors() const;

  // Sesión de juego.
  // En hardcore también se vigilan los logros conseguidos sólo en modo normal, para mejorarlos.
  void BeginSession(bool hardcore);
  bool SessionHardcore() const { return m_hardcore; }
  void EndSession();
  void DoFrame(const uint8_t* ram, size_t ram_size);
  // Logros recién desbloqueados desde la última llamada.
  std::vector<uint32_t> TakeNewUnlocks();
  Achievement* Find(uint32_t id);

private:
  std::string GameDir() const;
  bool LoadCache();
  void LoadUnlocks();
  void SaveUnlocks() const;
  void Unlock(uint32_t id);

public:
  void LogEvent(int type, uint32_t id);

private:
  void DownloadThread(std::string user, std::string password);

  State m_state = State::NoGame;
  std::string m_game_path;
  std::string m_hash;
  uint32_t m_game_id = 0;
  std::string m_game_title;
  std::vector<Achievement> m_list;

  // Estado de la descarga.
  std::atomic<bool> m_thread_done{false};
  bool m_thread_running = false;
  std::mutex m_mutex;
  std::string m_progress; // texto de estado
  std::string m_error;

  // Ejecución.
  void* m_runtime = nullptr; // rc_runtime_t*
  bool m_hardcore = false;
  const uint8_t* m_ram = nullptr;
  size_t m_ram_size = 0;
  std::vector<uint32_t> m_new_unlocks;
  std::vector<uint32_t> m_pending_deactivate;
};
