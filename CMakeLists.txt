cmake_minimum_required(VERSION 3.16)
project(tenshu2 C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(TP ${CMAKE_SOURCE_DIR}/third_party)
set(SDL2_DIR ${TP}/sdl2-install/lib/cmake/SDL2)
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
  src/audio.cpp
  src/stb_vorbis_impl.c)
target_include_directories(tenshu2 PRIVATE src ${TP}/libretro-common/include ${RC}/src/util)
target_link_libraries(tenshu2 PRIVATE SDL2::SDL2 rcheevos ${CMAKE_DL_LIBS} pthread)
target_compile_options(tenshu2 PRIVATE -Wall -Wno-unused-parameter)
# Sólo $ORIGIN/lib: el ejecutable es portable y no incrusta rutas de la máquina de compilación.
set_target_properties(tenshu2 PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${DIST}
  SKIP_BUILD_RPATH TRUE)
target_link_options(tenshu2 PRIVATE "-Wl,-rpath,\$ORIGIN/lib")

# Copia el núcleo, SDL2, recursos y licencias junto al ejecutable.
add_custom_command(TARGET tenshu2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST}/lib ${DIST}/core ${DIST}/licenses
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TP}/build-swan/swanstation_libretro.so ${DIST}/core/
  COMMAND sh -c "cp -P \"$0\"/libSDL2-2.0.so* \"$1\"/" ${TP}/sdl2-install/lib ${DIST}/lib
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${DIST}/assets
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TP}/swanstation/LICENSE ${DIST}/licenses/SwanStation-GPL-3.0.txt
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TP}/SDL2/LICENSE.txt ${DIST}/licenses/SDL2-zlib.txt
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RC}/LICENSE ${DIST}/licenses/rcheevos-MIT.txt
  VERBATIM)
