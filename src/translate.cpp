#include "translate.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <utility>
#include <vector>

namespace {

std::string Lower(std::string s)
{
  for (char& c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Nombres oficiales de las misiones en la versión española (leídos del propio disco).
// La clave va en minúsculas y sin el artículo inglés inicial.
std::string Mission(std::string en)
{
  static const std::pair<const char*, const char*> table[] = {
    {"training course", "Curso de entrenamiento"},
    {"gang of thieves", "La banda de ladrones"},
    {"treason at gohda castle", "Traición en Castillo Gohda"},
    {"lord toda's war camp", "Campo de guerra del Sr. Toda"},
    {"demon mountain", "La Montaña del Diablo"},
    {"secret harbor", "El puerto secreto"},
    {"temple of dreams", "El Templo de los Sueños"},
    {"ninja village under attack", "La aldea ninja es atacada"},
    {"in pursuit of tatsumaru", "Buscando a Tatsumaru"},
    {"kansen caverns", "Las Cavernas Kansen"},
    {"sea battle", "La batalla del mar"},
    {"mountain bandits", "Los bandidos de la montaña"},
    {"lady kei in danger", "La señora Kei en peligro"},
    {"to save a princess", "Salvar a una princesa"},
    {"kubon island", "La Isla Kubon"},
    {"island fort", "El fuerte de la isla"},
    {"quarantine village", "La aldea en cuarentena"},
    {"cherry tree hill", "La Colina de los Cerezos"},
    {"fire demon", "El Demonio de Fuego"},
    {"a shadow", "Una sombra"},
    {"head of lord toda", "La cabeza del señor Toda"},
    {"labor shortage", "Escasez de obreros"},
    {"guarding the secret harbor", "Proteger el puerto secreto"},
    {"no mercy", "Sin piedad"},
    {"assault on the ninja village", "Ataque a la aldea ninja"},
    {"final dawn", "El último amanecer"},
  };
  std::string key = Lower(en);
  if (key.rfind("the ", 0) == 0)
    key = key.substr(4);
  for (const auto& [k, v] : table)
    if (key == k)
      return v;
  return en; // misiones personalizadas del editor: se mantiene su nombre
}

std::string Quote(const std::string& s) { return "«" + s + "»"; }

// Condiciones finales que se repiten en muchas descripciones.
std::string Condition(const std::string& en)
{
  std::smatch m;
  const std::string t = R"((\d+:\d+(?:\.\d+)?))";
  if (std::regex_match(en, m, std::regex("(?:in |with a time of )?" + t + " or less")))
    return "en " + m[1].str() + " o menos";
  if (std::regex_match(en, m, std::regex("in less than " + t)))
    return "en menos de " + m[1].str();
  if (std::regex_match(en, m, std::regex("with a time of " + t)))
    return "en un tiempo de " + m[1].str();
  if (std::regex_match(en, m, std::regex(R"(in (\d+) moves or less)")))
    return "en " + m[1].str() + " movimientos o menos";
  if (en == "without killing anyone or being spotted")
    return "sin matar a nadie ni ser descubierto";
  if (en == "without killing anyone")
    return "sin matar a nadie";
  if (en == "without being spotted")
    return "sin ser descubierto";
  return {};
}

// Jefes y su epíteto, tal como aparecen en "Defeat X without taking damage".
std::string Boss(const std::string& en)
{
  static const std::pair<const char*, const char*> table[] = {
    {"Garan, Leader of the Thieves", "Garan, líder de los ladrones"},
    {"Motohide, Lord Gohda's Uncle", "Motohide, tío del señor Gohda"},
    {"Suzaku, The Red Sparrow at Lord Toda's War Camp", "Suzaku, el Gorrión Rojo, en «Campo de guerra del Sr. Toda»"},
    {"Kamadoma, Demon Ninja", "Kamadoma, el ninja demonio"},
    {"Wang Xiaohai of the Baoliusung Gang", "Wang Xiaohai, de la banda Baoliusung"},
    {"Yukihotaru, the Ice Firefly", "Yukihotaru, la Luciérnaga de Hielo"},
    {"Tatsumaru at the Ninja Village", "Tatsumaru en la aldea ninja"},
    {"Byakko, the White Tiger", "Byakko, el Tigre Blanco"},
    {"Suzaku, the Red Sparrow at The Sea Battle", "Suzaku, el Gorrión Rojo, en «La batalla del mar»"},
    {"Kagami, Leader of the Burning Dawn", "Kagami, líder del Amanecer Ardiente"},
    {"the slightly misnamed Boron, Leader of the Mountain Bandits",
     "Boron, líder de los bandidos de la montaña (con un nombre algo desafortunado)"},
    {"Genbu, Master of Ninja Sneaking on Lady Kei in Danger",
     "Genbu, maestro del sigilo ninja, en «La señora Kei en peligro»"},
    {"Wang Dahai, Head of the Baolisung", "Wang Dahai, jefe de los Baolisung"},
    {"the Three Servants of the Burning Dawn", "los tres sirvientes del Amanecer Ardiente"},
    {"Churro the Magnificent, the actual White Tiger", "Churro el Magnífico, el verdadero Tigre Blanco"},
    {"Genbu the Wise on the Fire Demon", "Genbu el Sabio en «El Demonio de Fuego»"},
    {"Tatsumaru on the Fire Demon", "Tatsumaru en «El Demonio de Fuego»"},
    {"Seiryu, the Blue Dragon", "Seiryu, el Dragón Azul"},
    {"Lord Toda", "al señor Toda"},
    {"Kagami after taking the Head of Lord Toda", "Kagami tras conseguir la cabeza del señor Toda"},
    {"Urana Takehito, Gohda Samurai", "Urana Takehito, samurái de Gohda"},
    {"Master Shiunsai", "al maestro Shiunsai"},
    {"Jubei, Lord Gohda's Fencing Instructor", "Jubei, instructor de esgrima del señor Gohda"},
  };
  for (const auto& [k, v] : table)
    if (en == k)
      return v;
  return en;
}

// Objetivos de la historia ("Eliminate The Gang of Thieves as Rikimaru"): misión y detalle extra.
bool StoryGoal(const std::string& goal, std::string* mission, std::string* extra)
{
  static const struct
  {
    const char* en;
    const char* mission;
    const char* extra;
  } table[] = {
    {"Eliminate The Gang of Thieves", "La banda de ladrones", ""},
    {"Defend Gohda Castle", "Traición en Castillo Gohda", ""},
    {"Infiltrate Lord Toda's War Camp", "Campo de guerra del Sr. Toda", ""},
    {"Free the Hostages on Demon Mountain", "La Montaña del Diablo", ""},
    {"Explore The Secret Harbor", "El puerto secreto", ""},
    {"Explore the Temple of Dreams", "El Templo de los Sueños", ""},
    {"Defend the Ninja Village", "La aldea ninja es atacada", ""},
    {"Pursue Tatsumaru", "Buscando a Tatsumaru", ""},
    {"Discover the secret of the Kansen Caverns", "Las Cavernas Kansen", ""},
    {"Discover the secret of The Kansen Caverns", "Las Cavernas Kansen", ""},
    {"Achieve victory in The Sea Battle", "La batalla del mar", ""},
    {"Eliminate The Mountain Bandits", "Los bandidos de la montaña", ""},
    {"Rush to the rescue of Lady Kei", "La señora Kei en peligro", ""},
    {"Save the Princess", "Salvar a una princesa", ""},
    {"Recon Kubon Island", "La Isla Kubon", ""},
    {"Rescue the villagers on The Island Fort", "El fuerte de la isla", ""},
    {"Defend the Quarantine Village", "La aldea en cuarentena", ""},
    {"Collect your thoughts on Cherry Tree Hill", "La Colina de los Cerezos", ""},
    {"Make your way through the Fire Demon and defeat Tatsumaru", "El Demonio de Fuego", "y derrota a Tatsumaru"},
    {"Chase down A Shadow", "Una sombra", ""},
    {"Take The Head of Lord Toda and defeat Kagami", "La cabeza del señor Toda", "y derrota a Kagami"},
    {"Solve the Burning Dawn's Labor Shortage", "Escasez de obreros", ""},
    {"Guard the Secret Harbor", "Proteger el puerto secreto", ""},
    {"Attack the Quarantine Village", "Sin piedad", ""},
    {"Assault the Ninja Village", "Ataque a la aldea ninja", ""},
  };
  for (const auto& e : table)
    if (goal == e.en)
    {
      *mission = e.mission;
      *extra = e.extra;
      return true;
    }
  return false;
}

} // namespace

std::string TranslateAchievementTitle(const std::string& title)
{
  static const std::pair<const char*, const char*> prefixes[] = {
    {"Swift as the Wind", "Veloz como el viento"}, {"Merciful Ninja", "Ninja compasivo"},
    {"Merciful Kunoichi", "Kunoichi compasiva"},   {"Merciful Master", "Maestro compasivo"},
    {"Shadow Assassin", "Asesino de las sombras"}, {"Wrath of Heaven", "Ira del cielo"},
    {"Fatal Shadow", "Sombra fatal"},
  };
  for (const auto& [en, es] : prefixes)
  {
    const std::string p = en;
    if (title.rfind(p, 0) == 0)
    {
      std::string rest = title.substr(p.size());
      if (rest == " Training")
        return std::string(es) + ": entrenamiento";
      const size_t t = rest.find("Training");
      if (t != std::string::npos)
        rest.replace(t, 8, "Entrenamiento");
      return es + rest;
    }
  }
  static const std::pair<const char*, const char*> exact[] = {
    {"Young Ninja", "Joven ninja"},       {"Young Kunoichi", "Joven kunoichi"},
    {"Azuma Ninja", "Ninja Azuma"},       {"Azuma Kunoichi", "Kunoichi Azuma"},
    {"Azuma Master", "Maestro Azuma"},    {"Pursuing the Traitor", "Persiguiendo al traidor"},
    {"Betrayal", "Traición"},             {"Reunion", "Reencuentro"},
    {"Awakening", "Despertar"},           {"Mighty Samurai", "Poderoso samurái"},
    {"Ninja Customs", "Costumbres ninja"}, {"A Real Fight", "Un combate de verdad"},
    {"Unforgivable Crimes", "Crímenes imperdonables"},
    {"Now I Am the Master", "Ahora el maestro soy yo"},
    {"Live in the Light", "Vivir en la luz"},
    {"I Have No Name", "No tengo nombre"},
    {"No Going Back", "Sin vuelta atrás"},
  };
  for (const auto& [en, es] : exact)
    if (title == en)
      return es;
  return title; // juegos de palabras y referencias: se conservan como nombre propio
}

std::string TranslateAchievementDescription(const std::string& d)
{
  std::smatch m;
  const std::string chr = "(Rikimaru|Ayame|Tatsumaru)";

  // Frases sueltas.
  if (d == "Complete all 16 custom missions in the mission editor")
    return "Completa las 16 misiones personalizadas del editor de misiones";
  if (d == "Complete A New Brand of Evil, eliminating all 6 enemies without being spotted")
    return "Completa «A New Brand of Evil» eliminando a los 6 enemigos sin ser descubierto";
  if (d == "Complete Tenchu, Incorporated without being spotted, killing all 5 enemies and with a time of 1:30 or less")
    return "Completa «Tenchu, Incorporated» sin ser descubierto, eliminando a los 5 enemigos y en 1:30 o menos";

  // Rango Gran Maestro en dificultad difícil.
  if (std::regex_match(d, m, std::regex("Achieve a Grand Master rating (?:on |in )?(.+) as " + chr +
                                        " on Hard Difficulty")))
    return "Consigue el rango Gran Maestro en " + Quote(Mission(m[1])) + " con " + m[2].str() +
           " en dificultad Difícil";

  // Asesinatos sigilosos.
  if (std::regex_match(d, m, std::regex("Perform all of " + chr + "'s stealth kills in a single mission")))
    return "Realiza todos los asesinatos sigilosos de " + m[1].str() + " en una sola misión";

  // Jefes sin recibir daño.
  if (std::regex_match(d, m, std::regex("Defeat (.+) without taking damage")))
  {
    const std::string boss = Boss(m[1]);
    const bool has_article = boss.rfind("al ", 0) == 0 || boss.rfind("los ", 0) == 0;
    // Si el jefe lleva título ("Garan, líder de…"), se cierra con coma antes de la condición.
    const bool titled = boss.find(',') != std::string::npos && boss.back() != ')';
    return std::string("Derrota ") + (has_article ? "" : "a ") + boss + (titled ? "," : "") + " sin recibir daño";
  }

  // Pruebas de Shiunsai (entrenamiento).
  if (std::regex_match(d, m, std::regex("Complete Shiunsai's (First|Second|Third|Fourth|Final) Test (.+)")))
  {
    static const std::pair<const char*, const char*> ord[] = {
      {"First", "primera"}, {"Second", "segunda"}, {"Third", "tercera"}, {"Fourth", "cuarta"}, {"Final", "última"}};
    std::string o;
    for (const auto& [en, es] : ord)
      if (m[1] == en)
        o = es;
    const std::string c = Condition(m[2]);
    if (!c.empty())
      return "Completa la " + o + " prueba de Shiunsai " + c;
  }

  // Misiones con personaje y condición.
  if (std::regex_match(d, m, std::regex("Complete (.+) as " + chr + " (.+)")))
  {
    const std::string c = Condition(m[3]);
    if (!c.empty())
      return "Completa " + Quote(Mission(m[1])) + " con " + m[2].str() + " " + c;
  }
  if (std::regex_match(d, m, std::regex("Complete (.+) as " + chr)))
    return "Completa " + Quote(Mission(m[1])) + " con " + m[2].str();

  // Objetivos de la historia.
  if (std::regex_match(d, m, std::regex("(.+) as " + chr)))
  {
    std::string mission, extra;
    if (StoryGoal(m[1], &mission, &extra))
      return "Completa " + Quote(mission) + " con " + m[2].str() + (extra.empty() ? "" : " " + extra);
  }

  // Misiones del editor con condición ("Complete Emancipation with a time of 2:00 or less").
  static const char* conds[] = {"with a time of", "without killing anyone", "without being spotted", "in "};
  if (d.rfind("Complete ", 0) == 0)
    for (const char* c : conds)
    {
      const size_t at = d.find(std::string(" ") + c, 9);
      if (at == std::string::npos)
        continue;
      const std::string cond = Condition(d.substr(at + 1));
      if (!cond.empty())
        return "Completa " + Quote(Mission(d.substr(9, at - 9))) + " " + cond;
    }

  return d;
}

std::string TranslateCheatName(const std::string& name)
{
  static const std::pair<const char*, const char*> table[] = {
    // Versiones europeas (inglés) y alemana.
    {"Infinite Air", "Aire infinito"},
    {"Infinite Air [ALT]", "Aire infinito (alternativo)"},
    {"Infinite Air (Alt)", "Aire infinito (alternativo)"},
    {"Infinite Health", "Salud infinita"},
    {"Infinite HP", "Vida infinita"},
    {"Tatsumaru unlocked", "Tatsumaru desbloqueado"},
    {"All levels unlocked", "Todas las misiones desbloqueadas"},
    {"Infinite All items", "Todos los objetos infinitos"},
    {"Ayame All items", "Ayame: todos los objetos"},
    {"Rikimaru All items", "Rikimaru: todos los objetos"},
    {"Tatsumaru All Items", "Tatsumaru: todos los objetos"},
    {"Ayame Records All perfect", "Ayame: récords perfectos"},
    {"Rikimaru Records All perfect", "Rikimaru: récords perfectos"},
    {"Tatsumaru Records All perfect", "Tatsumaru: récords perfectos"},
    {"Attach Grappling Hook Anywhere", "Gancho en cualquier superficie"},
    {"Invisible to Enemies", "Invisible para los enemigos"},
    {"Moon Jumps", "Saltos lunares"},
    {"Have Map", "Mapa siempre disponible"},
    {"Unlock All Extras", "Desbloquear todos los extras"},
    {"Mission Editor", "Editor de misiones"},
    {"Infinite Weapon Use", "Armas infinitas"},
    {"Unlock All Stages", "Todos los escenarios"},
    {"Unendlich Luft", "Aire infinito"},
    {"Unendlich Gesundheit", "Salud infinita"},
    {"Tatsumaru freigeschaltet", "Tatsumaru desbloqueado"},
    {"Alle Level", "Todas las misiones desbloqueadas"},
    {"Unendlich alle Gegenstände", "Todos los objetos infinitos"},
    {"Ayame Alles perfekt", "Ayame: récords perfectos"},
    {"Rikimaru Alle Gegenstände", "Rikimaru: todos los objetos"},
    {"Rikimaru Alles perfekt", "Rikimaru: récords perfectos"},
    {"Tatsumaru Alle Gegenstände", "Tatsumaru: todos los objetos"},
    {"Tatsumaru Alles perfekt", "Tatsumaru: récords perfectos"},
    // Versión de EE. UU. (récords por misión).
    {"Record", "Récords"},
    {"Grand Master Status", "Rango Gran Maestro"},
    {"Max Kills", "Bajas al máximo"},
    {"Max Score", "Puntuación máxima"},
    {"Time 0.00.0/Enables Mission", "Tiempo 0:00.0 (desbloquea la misión)"},
    {"Time 0.00.00/Enable Mission", "Tiempo 0:00.0 (desbloquea la misión)"},
    {"All Character", "Todos los personajes"},
    {"Have All Items", "Todos los objetos"},
    {"Unlock Tatsumaru", "Desbloquear a Tatsumaru"},
    {"Unlock All Mission Editor Levels", "Todos los escenarios del editor"},
    {"Tatsumaru-Everything Perfect", "Tatsumaru: todo perfecto"},
    // Versión japonesa.
    {"Dragon circle available", "Círculo del dragón disponible"},
    {"99 of each of the items in ninja tools", "99 de cada herramienta ninja"},
    {"Dragon circle and all mission Available", "Círculo del dragón y todas las misiones"},
    {"0 time required", "Tiempo 0"},
    {"255 of all ninja tools", "255 de cada herramienta ninja"},
    {"Infinite Physical strength .It is not valid for the death blow", "Vida infinita (no evita el golpe mortal)"},
    {"Time by 32 cloak of invisibility effect", "Capa de invisibilidad prolongada"},
    {"Cloak of invisibility effect is available in the ninja tools", "Capa de invisibilidad entre las herramientas"},
    {"Infinite Oxygen", "Aire infinito"},
    {"Infinite Time Cheat Sheet", "Tiempo infinito"},
    {"All Mission Cheat Sheet", "Todas las misiones"},
    {"Infinite Physical strength Cheat Sheet .It is not valid for the death blow.", "Vida infinita (alternativo)"},
    {"Infinite Oxygen Cheat Sheet", "Aire infinito (alternativo)"},
    {"Infinite Ninja tools Cheat Sheet", "Herramientas ninja infinitas"},
  };
  std::string out;
  size_t start = 0;
  while (start <= name.size())
  {
    const size_t end = name.find('\\', start);
    const std::string seg = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::string tr = seg;
    bool found = false;
    for (const auto& [en, es] : table)
      if (seg == en)
      {
        tr = es;
        found = true;
        break;
      }
    if (!found)
    {
      if (seg.rfind("Slot ", 0) == 0)
        tr = "Ranura " + seg.substr(5);
      else
        tr = Mission(seg); // nombres de misiones (o se mantiene tal cual)
    }
    out += (out.empty() ? "" : " · ") + tr;
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return out;
}
