#include "platform.h"
#include "i18n.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <vector>

namespace Platform {

FILE* OpenFile(const std::string& utf8_path, const char* mode)
{
#ifdef _WIN32
  return _wfopen(Path(utf8_path).wstring().c_str(), Path(mode).wstring().c_str());
#else
  return std::fopen(utf8_path.c_str(), mode);
#endif
}

bool ReadWholeFile(const std::string& utf8_path, std::string* out)
{
  FILE* f = OpenFile(utf8_path, "rb");
  if (!f)
    return false;
  out->clear();
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
    out->append(buf, n);
  std::fclose(f);
  return true;
}

bool RemoveFile(const std::string& utf8_path)
{
  std::error_code ec;
  return std::filesystem::remove(Path(utf8_path), ec);
}

bool RenameFile(const std::string& from, const std::string& to)
{
  std::error_code ec;
  std::filesystem::rename(Path(from), Path(to), ec);
  return !ec;
}

#ifdef _WIN32
static std::wstring Wide(const std::string& s)
{
  if (s.empty())
    return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
  return w;
}

static std::string Narrow(const std::wstring& w)
{
  if (w.empty())
    return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
  return s;
}

void* OpenLibrary(const std::string& path)
{
  std::wstring w = Wide(path);
  std::replace(w.begin(), w.end(), L'/', L'\\');
  return reinterpret_cast<void*>(LoadLibraryW(w.c_str()));
}

void* LibrarySymbol(void* lib, const char* name)
{
  return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
}

void CloseLibrary(void* lib)
{
  ::FreeLibrary(reinterpret_cast<HMODULE>(lib));
}

std::string LastLibraryError()
{
  return "error " + std::to_string(GetLastError());
}

FILE* OpenPipe(const std::string& command, const char* mode)
{
  // _popen ejecuta «cmd /c ...»: con comillas exteriores, cmd conserva las del interior.
  const std::string m = std::string(mode) + "b";
  return _wpopen(Wide("\"" + command + "\"").c_str(), Wide(m).c_str());
}

int ClosePipe(FILE* pipe)
{
  return _pclose(pipe);
}

std::string QuoteArg(const std::string& arg)
{
  std::string a = arg;
  std::replace(a.begin(), a.end(), '/', '\\');
  return "\"" + a + "\"";
}

const char* NullRedirect()
{
  return "2>NUL";
}

std::string OpenFileDialog(const std::string& title, const std::string& patterns)
{
  // Filtro: "título\0*.cue;*.bin\0Todos\0*.*\0\0".
  std::string list = patterns;
  std::replace(list.begin(), list.end(), ' ', ';');
  std::wstring filter = Wide(title);
  filter.push_back(L'\0');
  filter += Wide(list);
  filter.push_back(L'\0');
  filter += LangEn() ? L"All files" : L"Todos los archivos";
  filter.push_back(L'\0');
  filter += L"*.*";
  filter.push_back(L'\0');
  filter.push_back(L'\0');

  std::vector<wchar_t> file(32768, L'\0');
  const std::wstring wtitle = Wide(title);
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = filter.c_str();
  ofn.lpstrFile = file.data();
  ofn.nMaxFile = static_cast<DWORD>(file.size());
  ofn.lpstrTitle = wtitle.c_str();
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (!GetOpenFileNameW(&ofn))
    return {};
  std::string path = Narrow(file.data());
  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}
#else
void* OpenLibrary(const std::string& path)
{
  return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* LibrarySymbol(void* lib, const char* name)
{
  return dlsym(lib, name);
}

void CloseLibrary(void* lib)
{
  dlclose(lib);
}

std::string LastLibraryError()
{
  const char* e = dlerror();
  return e ? e : "";
}

FILE* OpenPipe(const std::string& command, const char* mode)
{
  return popen(command.c_str(), mode);
}

int ClosePipe(FILE* pipe)
{
  return pclose(pipe);
}

std::string QuoteArg(const std::string& arg)
{
  std::string q = "'";
  for (char c : arg)
    q += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return q + "'";
}

const char* NullRedirect()
{
  return "2>/dev/null";
}

std::string OpenFileDialog(const std::string&, const std::string&)
{
  return {};
}
#endif

} // namespace Platform
