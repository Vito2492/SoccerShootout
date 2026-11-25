# Shootout26 — Capcom's Soccer Shootout (SNES) Enhanced Frontend

Frontend nativo en **C + SDL2 + `snes9x_libretro`** para **Capcom's Soccer Shootout (USA)** (SNES, 1994) en **Windows (`.exe`)**, **Linux (`.AppImage`)** y **Android (`.apk`)**, con **3 packs de plantillas intercambiables en memoria (Actuales 2026, Leyendas Reales 1994 y Original 1994)**, accesos directos a los 6 modos de juego, modificadores de partido en vivo, soporte **2 Jugadores locales**, controles táctiles inteligentes en Android y menús totalmente accesibles con mando y pantalla táctil.

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

### Windows (`.exe`)
1. Descarga `SoccerShootout-Windows-x64.zip` desde **Releases** (o compila con `build_win.bat`).
2. Ejecuta `SoccerShootout.exe` y selecciona tu ROM `Capcom's Soccer Shootout (USA).sfc` (o `.smc`) en el diálogo que aparece la primera vez. Queda guardada en `soccer.cfg`.

### Linux (`.AppImage`)
```bash
chmod +x SoccerShootout-x86_64.AppImage
./SoccerShootout-x86_64.AppImage
```

### Android (`.apk`)
1. Instala `SoccerShootout-Android.apk`.
2. Al abrir por primera vez, elige tu ROM `.sfc`/`.smc` en el selector nativo de Android (SAF); se valida el CRC32 y arranca directo al menú.

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

## Compilación y CI (Windows + Linux + Android)

- **Windows local:** `build_win.bat` (MSVC 2022)
- **Linux / AppImage:** `make` y `make appimage`
- **GitHub Actions:** [`.github/workflows/build.yml`](.github/workflows/build.yml) genera automáticamente los binarios de **Windows**, **Linux AppImage** y **Android APK** en cada push y crea un Release completo al publicar un tag `v*`.
- Para firmar el APK de Android en modo release, configura los 4 secretos en GitHub (`KEYSTORE_BASE64`, `KEYSTORE_PASSWORD`, `KEY_ALIAS`, `KEY_PASSWORD`).
