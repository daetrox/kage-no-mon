# Tenshu 2 — Launcher

Lanzador para Linux que integra el núcleo de emulación **SwanStation** (libretro) dentro de una
aplicación propia: el juego arranca directamente desde el menú, sin interfaces ni mensajes del
emulador. Proyecto libre y sin fines de lucro.

## Qué incluye y qué no

| Incluido                                   | NO incluido (lo aporta cada usuario)            |
|--------------------------------------------|-------------------------------------------------|
| Launcher (GPL-3.0)                         | La imagen del disco del juego (carpeta `game/`) |
| Núcleo SwanStation (GPL-3.0)               | La BIOS de su consola (carpeta `bios/`)         |
| rcheevos (MIT), SDL2 (zlib)                | Su cuenta de RetroAchievements (para logros)    |
| Fuentes OFL / Apache-2.0                   |                                                 |

La música y los efectos son una composición original generada por código (`tools/make_audio.py`:
koto, shakuhachi, taiko y campana en escala in-sen) y se distribuyen bajo la misma licencia.

La interfaz se basa en la ilustración `tools/source.png`; `tools/make_assets.py` genera a partir
de ella el fondo, los botones, los iconos y la máscara del fuego animado (`assets/ui/`).

## Compilar

Requisitos: `git`, `cmake`, `ninja`, `g++` y las cabeceras de Wayland/OpenGL/PulseAudio
(para X11 hace falta también `libxext-dev`).

```sh
./build.sh             # resultado en build/dist/
./build.sh --package   # además genera tenshu2-linux-x86_64.tar.gz
```

## Descargar (AppImage)

En la pestaña **Releases** del repositorio hay un AppImage listo para cualquier distribución
moderna de Linux x86_64. Junto al archivo `.AppImage` crea:

```
Tenshu2-vX.Y-x86_64.AppImage
bios/   ← BIOS de tu consola (scph5502.bin, scph5501.bin…)
game/   ← imagen de tu disco (.cue + .bin, o .chd)
```

y ejecútalo (`chmod +x Tenshu2-*.AppImage && ./Tenshu2-*.AppImage`).

### Compilación automática

`packaging/appimage.yml` es el flujo de GitHub Actions que compila todo y genera el AppImage
(como artefacto en cada cambio y como Release al crear una etiqueta `vX.Y`). Para activarlo,
cópialo a `.github/workflows/appimage.yml`. En local: `./build.sh && packaging/make_appimage.sh`.

## Usar

```sh
./build/dist/tenshu2
```

- **Juego:** copia la imagen (`.cue/.bin/.chd`) en la carpeta `game/` junto al ejecutable; se
  detecta sola. También puedes elegirla en *OPCIONES › Imagen del juego* o
  arrastrar el archivo sobre la ventana.
- **BIOS:** copia la BIOS de tu consola en `bios/` (SCPH-5502 PAL, SCPH-5501 USA, SCPH-5500 JAP),
  o usa *OPCIONES › ISO y BIOS › Cargar BIOS nueva…*. En esa misma página se elige qué BIOS usar
  ("Automática" toma la de la región del disco). También puedes arrastrarla a la ventana.
- **Logros:** en *LOGROS* inicia sesión una vez con tu cuenta de RetroAchievements; se descargan
  el set y las insignias a `~/.local/share/tenshu2/achievements/`. A partir de ahí funcionan sin
  conexión: se evalúan en local y los desbloqueos se guardan en este equipo (no se envían a RA).
  La contraseña sólo se usa para obtener el token y no se guarda.
- **Resolución:** 4x por defecto. La escala 3x no se ofrece porque SwanStation corrompe el texto
  de Tenchu 2 a esa escala (1x, 2x, 4x y 5x verificadas). Filtro de texturas xBR por defecto.
- **Partidas:** las tarjetas de memoria se guardan en `~/.local/share/tenshu2/memcards`. Al
  volver al menú se crea un guardado automático que se retoma con **CONTINUAR**.
- **Pantalla panorámica:** se usa el parche 16:9 del propio juego de la base de datos comunitaria
  de DuckStation ([chtdb](https://github.com/duckstation/chtdb), autores El_Patas y PeterDelta)
  para las versiones SLES-02465 (España) y SLES-02452 (Europa). El truco panorámico del emulador
  no se usa porque descoloca el HUD de Tenchu 2. Sin parche, el juego se ve en 4:3 con los
  laterales decorados con la ilustración.
- **Música y sonidos:** música en el menú (más baja en la pausa, en silencio durante el juego) y
  efectos al moverse, elegir y cancelar. Volúmenes independientes en *OPCIONES*.
- **Configuración:** `~/.config/tenshu2/config.ini`.

### Controles

| Acción              | Teclado                | Mando              |
|---------------------|------------------------|--------------------|
| Cruceta             | Flechas                | Cruceta            |
| ✕ / ◯ / □ / △       | Z / X / A / S          | Botones posicionales (abajo = ✕) |
| L1 / R1 / L2 / R2   | Q / W / E / R          | Gatillos y bumpers |
| Start / Select      | Enter / Retroceso      | Start / Back       |
| Joysticks (DualShock) | —                    | Sticks             |
| Pausa               | Esc                    | L3 + R3 a la vez, o botón central (Guide/PS) |
| Guardar / cargar    | F5 / F7                | (desde la pausa)   |
| Pantalla completa   | F11 o Alt+Enter        | —                  |

## Estructura

```
src/main.cpp        Aplicación: menús, opciones, pausa, entrada, ventana sin bordes
src/emulator.cpp    Frontend libretro: carga el núcleo, vídeo (OpenGL/software), audio, input, estados
src/achievements.*  Logros offline (rcheevos): hash del disco, descarga única, evaluación y desbloqueos
src/audio.cpp       Música y efectos de la interfaz (stb_vorbis + mezclador SDL propio)
src/renderer.cpp    Dibujo 2D: texto (stb_truetype), imágenes (stb_image), figuras
assets/ui/          Fondo, botones, iconos y máscara de fuego (generados con tools/make_assets.py)
assets/shaders/     background.frag (fuego ondulante, calima, brasas) y ui.frag (paneles, iconos de mando)
assets/fonts/       Fuentes y sus licencias
```

Para modificar el arte, edita `assets/shaders/*.frag` con el launcher abierto y pulsa **F9** para
recargar los shaders sin reiniciar.

### Modo de prueba (desarrollo)

```sh
tenshu2 --screenshot salida.ppm --screen menu|options|controls|achievements|choice|game|continue --time SEGUNDOS
```

`TENSHU_DEBUG=1` muestra el registro completo del núcleo.

## Distribución y licencias

- Al distribuir binarios de un programa GPL hay que ofrecer también el código fuente: publica
  este directorio (sin `build/` ni `third_party/`) junto al paquete, y enlaza el código de
  SwanStation (`https://github.com/libretro/swanstation`, commit indicado en `build.sh`).
- El nombre y el texto bajo el logotipo se cambian en `src/main.cpp` (`kSubtitle`, `kTagline1/2`).
- No distribuyas la imagen del juego ni la BIOS de Sony.
