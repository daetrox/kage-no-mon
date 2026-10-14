// Idioma de la interfaz del launcher (español o inglés). Los textos se escriben en el propio sitio
// donde se usan, en los dos idiomas: L("Opciones", "Options").
#pragma once
#include <string>

enum class Lang { Es, En };

namespace I18n {
inline Lang& Current()
{
  static Lang lang = Lang::Es;
  return lang;
}
} // namespace I18n

inline bool LangEn() { return I18n::Current() == Lang::En; }
inline const char* L(const char* es, const char* en) { return LangEn() ? en : es; }
