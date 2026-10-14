#include "cheats.h"
#include "platform.h"
#include "translate.h"

#include <cctype>
#include <fstream>

namespace {

std::string Trim(const std::string& s)
{
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    b--;
  return s.substr(a, b - a);
}

// "AAAAAAAA VVVV" o "AAAAAAAA VVVVVVVV" en hexadecimal.
bool IsCodeLine(const std::string& l)
{
  const size_t sp = l.find(' ');
  if (sp != 8 || l.size() < 13 || l.size() > 17)
    return false;
  for (size_t i = 0; i < l.size(); i++)
    if (i != sp && !std::isxdigit(static_cast<unsigned char>(l[i])))
      return false;
  return true;
}

} // namespace

std::vector<Cheat> LoadCheats(const std::string& assets_dir, const std::string& serial)
{
  std::vector<Cheat> out;
  if (serial.empty())
    return out;
  std::ifstream in(Platform::Path(assets_dir + "/cheats/" + serial + ".cht"));
  std::string line;
  Cheat cur;
  auto flush = [&]() {
    // El aspecto de pantalla se controla en Gráficos: se omiten los trucos panorámicos.
    if (!cur.name.empty() && !cur.codes.empty() && cur.name.find("Widescreen") == std::string::npos)
    {
      cur.label = TranslateCheatName(cur.name);
      for (size_t i = 0; i < cur.name.size(); i++)
        cur.label_en += cur.name[i] == '\\' ? std::string(" · ") : std::string(1, cur.name[i]);
      out.push_back(cur);
    }
    cur = Cheat{};
  };
  while (std::getline(in, line))
  {
    line = Trim(line);
    if (line.empty() || line[0] == ';' || line[0] == '#')
      continue;
    if (line.front() == '[' && line.back() == ']')
    {
      flush();
      cur.name = line.substr(1, line.size() - 2);
      continue;
    }
    if (IsCodeLine(line))
      cur.codes += (cur.codes.empty() ? "" : "+") + line;
  }
  flush();
  return out;
}
