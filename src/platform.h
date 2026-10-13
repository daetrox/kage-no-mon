// Lo que depende del sistema operativo (Linux / Windows), para que el resto del código no tenga
// que incluir <windows.h> (sus macros, como CopyFile o DrawText, chocan con nombres del proyecto).
#pragma once
#include <cstdio>
#include <string>

namespace Platform {
// Bibliotecas dinámicas (el núcleo libretro).
void* OpenLibrary(const std::string& path);
void* LibrarySymbol(void* lib, const char* name);
void CloseLibrary(void* lib);
std::string LastLibraryError();

// Ejecuta una orden y abre su entrada o salida estándar ("r" / "w"); ClosePipe devuelve su código.
FILE* OpenPipe(const std::string& command, const char* mode);
int ClosePipe(FILE* pipe);
// Pone una ruta entre comillas para el intérprete de órdenes del sistema.
std::string QuoteArg(const std::string& arg);
// Destino de la salida que no interesa ("2>/dev/null" o "2>NUL").
const char* NullRedirect();

// Diálogo nativo de Windows para elegir un archivo (ruta en UTF-8 con '/'; "" si se cancela).
// patterns: "*.cue *.bin ..." (como en zenity).
std::string OpenFileDialog(const std::string& title, const std::string& patterns);
} // namespace Platform
