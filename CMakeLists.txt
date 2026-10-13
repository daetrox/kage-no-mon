cmake_minimum_required(VERSION 3.16)
project(tenshu2 C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(TP ${CMAKE_SOURCE_DIR}/third_party)
if(WIN32)
  # Windows: SDL2 oficial para MinGW y el núcleo compilado aparte (build-windows.sh).
  set(SDL2_DIR ${TP}/sdl2-win/x86_64-w64-mingw32/lib/cmake/SDL2)
  set(SWAN_BUILD ${TP}/build-swan-win)
  set(SDL_LICENSE ${TP}/sdl2-win/LICENSE.txt)
else()
  set(SDL2_DIR ${TP}/sdl2-install/lib/cmake/SDL2)
  set(SWAN_BUILD ${TP}/build-swan)
  set(SDL_LICENSE ${TP}/SDL2/LICENSE.txt)
endif()
find_package(SDL2 REQUIRED CONFIG)

set(DIST ${CMAKE_BINARY_DIR}/dist)

# rcheevos (MIT): cálculo de hash del disco y evaluación de logros.
set(RC ${TP}/rcheevos)
file(GLOB RC_SOURCES ${RC}/src/rc_*.c ${RC}/src/runtime/*.c ${RC}/src/util/*.c
     ${RC}/src/hash/rc_hash.c ${RC}/src/hash/rc_hash_disc.c ${RC}/src/hash/rc_cdreader.c)
add_library(rcheevos STATIC ${RC_SOURCES})
target_include_directories(rcheevos PUBLIC ${RC}/include PRIVATE ${RC}/src)
target_compile_definitions(rcheevos PUBLIC RC_DISABLE_LUA RC_HASH_NO_ROM RC_HASH_NO_ENCRYPTED RC_HASH_NO_ZIP)
set_target_properties(rcheevos PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(tenshu2
  src/main.cpp
  src/emulator.cpp
  src/renderer.cpp
  src/config.cpp
  src/gl.cpp
  src/achievements.cpp
  src/translate.cpp
  src/cheats.cpp
  src/game_buttons.cpp
  src/free_camera.cpp
  src/audio.cpp
  src/platform.cpp
  src/stb_vorbis_impl.c)
target_include_directories(tenshu2 PRIVATE src ${TP}/libretro-common/include ${RC}/src/util)
target_compile_options(tenshu2 PRIVATE -Wall -Wno-unused-parameter)
set_target_properties(tenshu2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${DIST})
if(WIN32)
  # KageNoMon.exe: aplicación de ventana (sin consola), runtime de C++ enlazado dentro y recursos
  # (icono, manifiesto, versión).
  file(STRINGS ${CMAKE_SOURCE_DIR}/src/main.cpp _ver REGEX "kVersion = \"v[0-9]+\\.[0-9]+\\.[0-9]+\"")
  string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ver "${_ver}")
  set(V_MAJOR ${CMAKE_MATCH_1})
  set(V_MINOR ${CMAKE_MATCH_2})
  set(V_PATCH ${CMAKE_MATCH_3})
  set(ICON ${CMAKE_SOURCE_DIR}/packaging/windows/kagenomon.ico)
  set(MANIFEST ${CMAKE_SOURCE_DIR}/packaging/windows/kagenomon.manifest)
  configure_file(${CMAKE_SOURCE_DIR}/packaging/windows/kagenomon.rc.in ${CMAKE_BINARY_DIR}/kagenomon.rc @ONLY)
  target_sources(tenshu2 PRIVATE ${CMAKE_BINARY_DIR}/kagenomon.rc)
  target_link_libraries(tenshu2 PRIVATE SDL2::SDL2main SDL2::SDL2 rcheevos comdlg32)
  target_link_options(tenshu2 PRIVATE -static -mwindows)
  set_target_properties(tenshu2 PROPERTIES OUTPUT_NAME KageNoMon)
else()
  target_link_libraries(tenshu2 PRIVATE SDL2::SDL2 rcheevos ${CMAKE_DL_LIBS} pthread)
  # Sólo $ORIGIN/lib: el ejecutable es portable y no incrusta rutas de la máquina de compilación.
  set_target_properties(tenshu2 PROPERTIES SKIP_BUILD_RPATH TRUE)
  target_link_options(tenshu2 PRIVATE "-Wl,-rpath,\$ORIGIN/lib")
endif()

# Copia el núcleo, SDL2, recursos y licencias junto al ejecutable.
if(WIN32)
  add_custom_command(TARGET tenshu2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST}/core ${DIST}/licenses
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SWAN_BUILD}/swanstation_libretro.dll ${DIST}/core/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TP}/sdl2-win/x86_64-w64-mingw32/bin/SDL2.dll ${DIST}/
    VERBATIM)
else()
  add_custom_command(TARGET tenshu2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST}/lib ${DIST}/core
    COMMAND sh -c "cp -P \"$0\"/libSDL2-2.0.so* \"$1\"/" ${TP}/sdl2-install/lib ${DIST}/lib
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SWAN_BUILD}/swanstation_libretro.so ${DIST}/core/
    VERBATIM)
endif()
add_custom_command(TARGET tenshu2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST}/core ${DIST}/licenses
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${DIST}/assets
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TP}/swanstation/LICENSE ${DIST}/licenses/SwanStation-GPL-3.0.txt
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL_LICENSE} ${DIST}/licenses/SDL2-zlib.txt
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RC}/LICENSE ${DIST}/licenses/rcheevos-MIT.txt
  VERBATIM)
