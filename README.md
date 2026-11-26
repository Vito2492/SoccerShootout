# Shootout26 — Capcom's Soccer Shootout (SNES) Enhanced Frontend

Frontend nativo en **C + SDL2 + `snes9x_libretro`** para **Capcom's Soccer Shootout (USA)** (SNES, 1994) en **Windows (`.exe`)**, **Linux** y **Android (`.apk`)**, con **3 packs de plantillas intercambiables en memoria (Actuales 2026, Leyendas Reales 1994 y Original 1994)**, accesos directos a los 6 modos de juego, modificadores de partido en vivo, soporte **2 Jugadores locales**, controles táctiles inteligentes en Android y menús totalmente accesibles con mando y pantalla táctil.

> **Aviso legal:** Este repositorio **no incluye ninguna ROM ni assets con copyright**. Al abrir el programa por primera vez, enlazas tu propia ROM dumpeada legalmente de *Capcom's Soccer Shootout (USA)* (1.5 MB, CRC32 `0xEE314AE5`). El archivo de la ROM en disco nunca se modifica: los packs de plantillas se aplican dinámicamente en memoria RAM antes de cargar el núcleo.

---

## Características principales

- **3 Packs de Plantillas intercambiables (12 selecciones × 20 jugadores = 240 futbolistas):**
  - **ACTUAL 2026:** selecciones actualizadas (*Messi, Lautaro, Julián, Dibu, Mbappé, Dembélé, Vinícius, Raphinha, Alisson, Yamal, Pedri, Rodri, Bellingham, Kane, Musiala, Wirtz, Pulisic, Giménez, Van Dijk, Donnarumma, Onana...*).
  - **LEYENDAS 1994:** los nombres reales históricos del Mundial USA '94 reemplazando los seudónimos sin licencia de 1994 (*Romário, Bebeto, Taffarel, Maradona, Batistuta, Caniggia, R. Baggio, Maldini, Klinsmann, Matthäus, Cantona, Bergkamp, Hierro, Campos, Hugo Sánchez, Roger Milla, Lalas...*).
  - **ORIGINAL 1994:** la tabla intacta de Capcom.
  - Los nombres reales aparecen tanto en la pantalla de alineaciones como en el **HUD en vivo durante el partido**, y además puedes explorar las 12 plantillas desde **VER PLANTILLAS**.
- **Arte 100% extraído de tu ROM al iniciar:**
  - Fondo violeta clásico con marca de agua CAPCOM, cursor triangular dorado y fuente 8×8 (`A–Z`, `0–9`) extraídos directamente de la VRAM de tu propia ROM.
- **Accesos directos a los 6 modos desde el Menú de Inicio:**
  - `EXHIBICION`, `TEMPORADA` (Main Game), `ALL-STARS`, `PENALES (PK)`, `INDOOR 8V8` y `ENTRENAMIENTO`.
- **Modificadores de partido en vivo (`PAUSA > MODIFICADORES`, sin reiniciar):**
  - **Reloj Congelado** (pausa el cronómetro del partido indefinidamente).
  - **Gol de Oro** (muerte súbita: el próximo gol pita el final automático con aviso del ganador).
  - **Velocidad Turbo** (ritmo arcade acelerado) y **Sin Slowdown** (overclock compatible de Snes9x).
  - **Control del marcador en vivo:** `+1 GOL P1`, `+1 GOL P2`, `MARCADOR 0-0`, `+1 MINUTO` (ideal para entrenar remontadas o forzar penales).
- **2 Jugadores locales (1P vs 2P / Co-op):**
  - Soporte nativo para **2 mandos simultáneos** (Mando 1 = P1, Mando 2 = P2) + teclado dual (`Flechas/ZXAS` para P1 y `IJKL/NMHUO` para P2).
- **Android con pantalla táctil inteligente y soporte de mando:**
  - Botones virtuales multitouch pensados para fútbol (`D-Pad 8 direcciones`, `PASS`, `SHOT`, `LOB`, `DASH`, `L/R`, `SEL/STRT`, `MENU`, `SAVE`, `LOAD`).
  - Modo `TACTIL: AUTO` que **oculta el pad virtual al conectar/usar mando físico** dejando siempre un botón `MENU` discreto arriba a la izquierda.
  - Menús totalmente navegables por **toque directo**, **D-Pad**, **stick analógico** o **ratón**.

---

## Cómo usar

### Windows
1. Descarga [SoccerShootout-Windows.zip](https://github.com/Vito2492/SoccerShootout/releases/latest/download/SoccerShootout-Windows.zip) y descomprímelo.
2. Abre `SoccerShootout.exe`. La primera vez elige tu ROM `Capcom's Soccer Shootout (USA).sfc` (`.sfc` o `.smc`), o déjala junto al `.exe`.

### Linux
1. Descarga [SoccerShootout-Linux.tar.gz](https://github.com/Vito2492/SoccerShootout/releases/latest/download/SoccerShootout-Linux.tar.gz).
2. Descomprime y ejecuta (trae su propia copia de SDL2 en `lib/`):
   ```bash
   tar xzf SoccerShootout-Linux.tar.gz && cd SoccerShootout-Linux
   ./soccer-shootout "ruta/a/Capcom's Soccer Shootout (USA).sfc"
   ```

### Android
1. Descarga e instala [SoccerShootout-Android.apk](https://github.com/Vito2492/SoccerShootout/releases/latest/download/SoccerShootout-Android.apk) desde el celular (hay que permitir instalar apps de origen desconocido).
2. La primera vez te pide elegir tu ROM (`.sfc`, `.smc` o `.zip`); se valida y queda guardada dentro de la app.
3. Las versiones nuevas se instalan encima y conservan las partidas. Juega con la pantalla táctil o con un mando Bluetooth/USB.

> Ningún paquete incluye el juego: necesitas tu propia copia (versión USA).

---

## Controles

| Acción | P1 Teclado | P2 Teclado | Mando (P1 / P2) | Pantalla táctil (Android) |
|---|---|---|---|---|
| Mover | Flechas | `I / K / J / L` | D-Pad / Stick izq. | D-Pad izquierdo (8 dir.) |
| Pase (`Y`) / Tiro (`B`) | `A` / `Z` | `H` / `N` | `X` / `A` | `PASS` / `SHOT` |
| Globito (`A`) / Turbo (`X`) | `X` / `S` | `M` / `U` | `B` / `Y` | `LOB` / `DASH` |
| `L` / `R` | `Q` / `W` | — | `LB` / `RB` | `L` / `R` |
| `Start` / `Select` | `Enter` / `Shift Der.` | `O` | `Start` / `Back` | `STRT` / `SEL` |
| **Menú de Pausa** | `F1` o `Esc` | — | `Back`, `Guide`, `Select+Start`, `LB+RB+Start`, Atrás Android | Botón `MENU` (arriba izq.) |
| Guardar / Cargar Estado | `F5` / `F7` | — | `LT`/`RT` guarda · `L3`/`R3` carga | `SAVE` / `LOAD` |

---

## Compilación desde código fuente

- **Linux:** `sudo apt install libsdl2-dev && make` (pon `snes9x_libretro.so` junto al programa).
- **Windows (MinGW, igual que el CI):**
  ```bash
  x86_64-w64-mingw32-gcc -O2 -Isrc -I$SDL/include/SDL2 -I$SDL/include src/soccer.c -o SoccerShootout.exe \
    -L$SDL/lib -lmingw32 -lSDL2main -lSDL2 -lcomdlg32 -luser32 -lshell32 -lm -mwindows
  ```
  (o `build_win.bat` con MSVC 2022). Junto al `.exe` van `SDL2.dll` y `snes9x_libretro.dll`.
- **Android:** `tools/android-prepare.sh SDL2-2.30.9 snes9x/libretro/libs` y luego `cd android && gradle assembleRelease` (el workflow muestra los pasos completos).
- **GitHub Actions:** [`.github/workflows/build.yml`](.github/workflows/build.yml) compila Windows, Linux y Android (arm64-v8a, armeabi-v7a, x86_64) en cada push a `main`, con SDL2 2.30.9 y snes9x compilado desde su código fuente, y publica o actualiza el release `v` + [`VERSION`](VERSION).
- **Firma del APK:** con los secretos `ANDROID_KEYSTORE_B64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS` y `ANDROID_KEY_PASSWORD` el APK sale firmado siempre con la misma clave, así cada versión se instala encima de la anterior. Sin ellos se compila igual, pero el APK no se publica.


