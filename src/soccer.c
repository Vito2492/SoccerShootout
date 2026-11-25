// Shootout26: native cross-platform frontend for the SNES game
// Capcom's Soccer Shootout (USA) (1994, Capcom / A-Max).
// Runs the player's own legally dumped ROM through snes9x_libretro, extracts
// its menu art, font, and cursor from that ROM at start-up, applies dynamic
// in-memory Roster Packs (Modern 2026, Real 1994 Legends, or Original 1994),
// and adds Quick Modes, Live Match Modifiers (Freeze Clock, Golden Goal,
// Turbo, Scoreboard), Roster Viewer, 2-Player Local Support, Save States,
// On-Screen Touch Controls for Android, Accessible Gamepad Menus, Native ROM
// Picker, and Spanish/English UI. The ROM file on disk is never modified.

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "libretro.h"
#include "rosters.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#define CORE_NAME "snes9x_libretro.dll"
#elif defined(__ANDROID__)
#define CORE_NAME "libsnes9x_libretro.so"
#else
#define CORE_NAME "snes9x_libretro.so"
#endif

#define W 256
#define H 224
#define ROM_SIZE 0x180000u
#define ROM_CRC  0xee314ae5u

/* ------------------------------------------------------------------ */
/* Core                                                                 */
/* ------------------------------------------------------------------ */

static struct {
    void (*init)(void), (*deinit)(void), (*run)(void), (*reset)(void);
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    bool (*load_game)(const struct retro_game_info *);
    void (*unload_game)(void);
    void (*get_av)(struct retro_system_av_info *);
    void *(*mem_data)(unsigned);
    size_t (*mem_size)(unsigned);
    size_t (*ser_size)(void);
    bool (*serialize)(void *, size_t);
    bool (*unserialize)(const void *, size_t);
} core;

static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static uint32_t game[512 * 512];
static int gw = W, gh = H;
static uint16_t pad_p1, pad_p2;
static bool layer_on[5] = { true, true, true, true, true };
static bool layers_dirty;
static bool overclock;
static bool scripting;
static char layer_val[5][16];
static SDL_AudioDeviceID audio;
static bool mute;
static char base_dir[1024];
static char save_dir[1024];

// Working ROM copy in memory + original roster tables (T1 pitch HUD + T2 lineup) for instant 3-pack switching
static uint8_t *rom_work;
static uint8_t  orig_roster[ROSTER_TEAMS * ROSTER_PLAYERS * ROSTER_NAME_LEN];
static uint8_t  orig_roster_t2[ROSTER_T2_SIZE];

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd & 0xffff) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixfmt = *(enum retro_pixel_format *)data;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = save_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = (struct retro_variable *)data;
        int n;
        if (sscanf(v->key, "snes9x_layer_%d", &n) == 1 && n >= 1 && n <= 5) {
            strcpy(layer_val[n - 1], layer_on[n - 1] ? "enabled" : "disabled");
            v->value = layer_val[n - 1];
            return true;
        }
        if (!strcmp(v->key, "snes9x_overclock_cycles")) {
            v->value = overclock && !scripting ? "compatible" : "disabled";
            return true;
        }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = layers_dirty;
        layers_dirty = false;
        return true;
    }
    return false;
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || w > 512 || h > 512) return;
    gw = (int)w; gh = (int)h;
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *row = (const uint8_t *)data + y * pitch;
        uint32_t *out = game + y * w;
        for (unsigned x = 0; x < w; x++) {
            uint32_t r, g, b;
            if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
                out[x] = 0xff000000u | ((const uint32_t *)row)[x];
                continue;
            }
            uint16_t p = ((const uint16_t *)row)[x];
            if (pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
                r = p >> 11; g = (p >> 5) & 63; b = p & 31;
                r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
            } else {
                r = (p >> 10) & 31; g = (p >> 5) & 31; b = p & 31;
                r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
            }
            out[x] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
}

static size_t audio_batch_cb(const int16_t *d, size_t frames)
{
    if (audio && !mute) SDL_QueueAudio(audio, d, (Uint32)(frames * 4));
    return frames;
}
static void audio_cb(int16_t l, int16_t r) { int16_t s[2] = { l, r }; audio_batch_cb(s, 1); }
static void poll_cb(void) {}
static int16_t input_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (device != RETRO_DEVICE_JOYPAD) return 0;
    uint16_t p = (port == 0) ? pad_p1 : (port == 1 ? pad_p2 : 0);
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)p;
    return id < 16 ? (int16_t)((p >> id) & 1) : 0;
}

static void set_layers(int only)
{
    for (int i = 0; i < 5; i++) layer_on[i] = !only || i + 1 == only;
    layers_dirty = true;
}

static uint8_t *wram(void) { return (uint8_t *)core.mem_data(RETRO_MEMORY_SYSTEM_RAM); }

static bool load_core(const char *path)
{
    void *so = SDL_LoadObject(path);
    if (!so) return false;
#define SYM(field, name) if (!(*(void **)&core.field = SDL_LoadFunction(so, name))) return false
    SYM(init, "retro_init"); SYM(deinit, "retro_deinit"); SYM(run, "retro_run"); SYM(reset, "retro_reset");
    SYM(set_environment, "retro_set_environment"); SYM(set_video_refresh, "retro_set_video_refresh");
    SYM(set_audio_sample, "retro_set_audio_sample"); SYM(set_audio_sample_batch, "retro_set_audio_sample_batch");
    SYM(set_input_poll, "retro_set_input_poll"); SYM(set_input_state, "retro_set_input_state");
    SYM(load_game, "retro_load_game"); SYM(unload_game, "retro_unload_game");
    SYM(get_av, "retro_get_system_av_info");
    SYM(mem_data, "retro_get_memory_data"); SYM(mem_size, "retro_get_memory_size");
    SYM(ser_size, "retro_serialize_size"); SYM(serialize, "retro_serialize"); SYM(unserialize, "retro_unserialize");
#undef SYM
    core.set_environment(env_cb);
    core.init();
    core.set_video_refresh(video_cb);
    core.set_audio_sample(audio_cb);
    core.set_audio_sample_batch(audio_batch_cb);
    core.set_input_poll(poll_cb);
    core.set_input_state(input_cb);
    return true;
}

/* ------------------------------------------------------------------ */
/* Art & Font extracted dynamically from the player's ROM               */
/* ------------------------------------------------------------------ */

static uint32_t bg[W * H];           // Royal purple CAPCOM tiled wallpaper + stadium silver frame
static uint8_t  font[64][8][8];      // ASCII 0x20-0x5F, 8x8 px, values 0-3
static uint32_t cursor[32 * 32];     // Golden triangle cursor from Exhibition Options (8x14)
static int curw, curh;
static void *mode_snap;
static size_t mode_snap_size;

static void run(int n) { while (n--) core.run(); }
static void tap(int btn, int wait) { pad_p1 = (uint16_t)(1u << btn); run(4); pad_p1 = 0; run(wait); }

static void boot_to_mode_select(void)
{
    if (mode_snap && core.unserialize(mode_snap, mode_snap_size)) return;
    core.reset();
    run(60 * 10);
    tap(RETRO_DEVICE_ID_JOYPAD_START, 60);
    tap(RETRO_DEVICE_ID_JOYPAD_START, 90);
}

static void set_glyph(char ch, const char *rows[8])
{
    int idx = (unsigned char)ch - 0x20;
    if (idx < 0 || idx >= 64) return;
    memset(font[idx], 0, sizeof font[idx]);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8 && rows[y][x]; x++)
            if (rows[y][x] == '#') {
                font[idx][y][x] = 3;
                if (x + 1 < 8 && font[idx][y][x + 1] == 0) font[idx][y][x + 1] = 1;
                if (y + 1 < 8 && font[idx][y + 1][x] == 0) font[idx][y + 1][x] = 1;
            }
}

static void fill_punctuation(void)
{
    static const char *g_colon[8] = { "........", "...##...", "...##...", "........", "...##...", "...##...", "........", "........" };
    static const char *g_dot[8]   = { "........", "........", "........", "........", "........", "...##...", "...##...", "........" };
    static const char *g_dash[8]  = { "........", "........", "........", ".#####..", ".#####..", "........", "........", "........" };
    static const char *g_plus[8]  = { "........", "...#....", "...#....", ".#####..", "...#....", "...#....", "........", "........" };
    static const char *g_slash[8] = { ".....#..", "....##..", "...##...", "..##....", ".##.....", "#.......", "........", "........" };
    static const char *g_lp[8]    = { "...##...", "..##....", "..##....", "..##....", "..##....", "...##...", "........", "........" };
    static const char *g_rp[8]    = { "..##....", "...##...", "...##...", "...##...", "...##...", "..##....", "........", "........" };
    static const char *g_excl[8]  = { "...##...", "...##...", "...##...", "...##...", "........", "...##...", "...##...", "........" };
    static const char *g_q[8]     = { ".####...", "##..##..", "...##...", "..##....", "........", "..##....", "..##....", "........" };
    static const char *g_pct[8]   = { ".##..#..", ".##.##..", "...##...", "..##....", ".##.##..", "#...##..", "........", "........" };
    static const char *g_star[8]  = { "...#....", ".#.#.#..", "..###...", ".#.#.#..", "...#....", "........", "........", "........" };
    static const char *g_apos[8]  = { "...##...", "...##...", "..##....", "........", "........", "........", "........", "........" };
    static const char *g_lt[8]    = { "....##..", "...##...", "..##....", "...##...", "....##..", "........", "........", "........" };
    static const char *g_gt[8]    = { "..##....", "...##...", "....##..", "...##...", "..##....", "........", "........", "........" };
    set_glyph(':', g_colon); set_glyph('.', g_dot); set_glyph('-', g_dash);
    set_glyph('+', g_plus);  set_glyph('/', g_slash); set_glyph('(', g_lp);
    set_glyph(')', g_rp);    set_glyph('!', g_excl);  set_glyph('?', g_q);
    set_glyph('%', g_pct);   set_glyph('*', g_star);  set_glyph('\'', g_apos);
    set_glyph('<', g_lt);    set_glyph('>', g_gt);
}

static bool extract_art(void)
{
    mute = true; scripting = true; layers_dirty = true;
    memset(font, 0, sizeof font);
    fill_punctuation();

    // 1) Boot to -MODE SELECT- and snapshot it for instant mode launching
    free(mode_snap); mode_snap = NULL;
    boot_to_mode_select();
    mode_snap_size = core.ser_size();
    mode_snap = malloc(mode_snap_size);
    if (mode_snap && !core.serialize(mode_snap, mode_snap_size)) { free(mode_snap); mode_snap = NULL; }

    // 2) Enter Exhibition -> PLAYERS screen: grab clean CAPCOM wallpaper (L2), golden cursor (L5), and 2bpp 8x8 font!
    tap(RETRO_DEVICE_ID_JOYPAD_A, 120);

    set_layers(2); run(2);
    // Clean purple CAPCOM tiled wallpaper: tile rows 0..111 cleanly across the full 224 height
    for (int y = 0; y < H; y++) {
        int sy = y < 112 ? y : (y - 112);
        memcpy(bg + y * W, game + sy * gw, W * 4);
    }

    // Grab golden triangle cursor from Layer 5 (x=64..71, y=57..70)
    set_layers(5); run(2);
    uint32_t bg0 = game[0];
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 52; y < 80; y++)
        for (int x = 58; x < 78; x++)
            if (game[y * gw + x] != bg0) {
                if (x < x0) x0 = x; if (x > x1) x1 = x;
                if (y < y0) y0 = y; if (y > y1) y1 = y;
            }
    if (x1 < 0 || x1 - x0 >= 32 || y1 - y0 >= 32) {
        // Fallback crisp 7x9 golden arrow if timing shifted
        curw = 7; curh = 9;
        memset(cursor, 0, sizeof cursor);
        for (int y = 0; y < 9; y++) {
            int span = (y <= 4) ? (y + 1) : (9 - y);
            for (int x = 0; x < span; x++) cursor[y * curw + x] = 0xffffd830u;
        }
    } else {
        curw = x1 - x0 + 1; curh = y1 - y0 + 1;
        for (int y = 0; y < curh; y++)
            for (int x = 0; x < curw; x++) {
                uint32_t p = game[(y0 + y) * gw + x0 + x];
                cursor[y * curw + x] = (p != bg0) ? p : 0;
            }
    }

    // Grab 2bpp 8x8 A..Z (VRAM 0x7800, 26 tiles) and 0..9 (VRAM 0x7400, 10 tiles)
    const uint8_t *v = (const uint8_t *)core.mem_data(RETRO_MEMORY_VIDEO_RAM);
    if (!v || core.mem_size(RETRO_MEMORY_VIDEO_RAM) < 0x10000) goto fail;
    int ink = 0;
    for (int ch = 0; ch < 26; ch++) {
        const uint8_t *t = v + 0x7800 + ch * 16;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                uint8_t val = (uint8_t)(((t[y * 2] >> (7 - x)) & 1) | (((t[y * 2 + 1] >> (7 - x)) & 1) << 1));
                font[('A' - 0x20) + ch][y][x] = val;
                if (val == 3) ink++;
            }
    }
    // Digits 0..9 in VRAM 0x7400 (top tile) + 0x7500 (bottom tile): sample 8 balanced rows for crisp full 8x8 digits
    static const int drow[8] = { 1, 3, 5, 7, 9, 11, 13, 14 };
    for (int d = 0; d < 10; d++) {
        const uint8_t *tu = v + 0x7400 + d * 16;
        const uint8_t *tl = v + 0x7500 + d * 16;
        for (int y = 0; y < 8; y++) {
            int r = drow[y];
            const uint8_t *src = (r < 8) ? (tu + r * 2) : (tl + (r - 8) * 2);
            for (int x = 0; x < 8; x++) {
                uint8_t val = (uint8_t)(((src[0] >> (7 - x)) & 1) | (((src[1] >> (7 - x)) & 1) << 1));
                font[('0' - 0x20) + d][y][x] = val;
            }
        }
    }
    if (ink < 120) goto fail;

    set_layers(0);
    boot_to_mode_select();
    scripting = false; mute = false; layers_dirty = true;
    return true;
fail:
    set_layers(0);
    scripting = false; mute = false; layers_dirty = true;
    return false;
}

/* ------------------------------------------------------------------ */
/* Drawing on the 256x224 menu layer                                    */
/* ------------------------------------------------------------------ */

static uint32_t ui[W * H];
typedef struct { uint32_t c[4]; } pal_t;
static const pal_t WHITE  = { { 0, 0xff080818, 0xffccd8e8, 0xffffffff } };
static const pal_t GOLD   = { { 0, 0xff180800, 0xffd89818, 0xffffea40 } };
static const pal_t GREEN  = { { 0, 0xff041808, 0xff28b858, 0xff70ff98 } };
static const pal_t CYAN   = { { 0, 0xff041020, 0xff3898d8, 0xff78e8ff } };
static const pal_t RED    = { { 0, 0xff180404, 0xffc82828, 0xffff5848 } };
static const pal_t DIM    = { { 0, 0xff080818, 0xff6a7ca8, 0xff98b0dc } };

static void text(int x, int y, const char *s, pal_t p)
{
    for (; *s; s++, x += 8) {
        int c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 0x20 || c >= 0x60) c = '?';
        for (int yy = 0; yy < 8; yy++)
            for (int xx = 0; xx < 8; xx++) {
                int v = font[c - 0x20][yy][xx], px = x + xx, py = y + yy;
                if (v && px >= 0 && px < W && py >= 0 && py < H) ui[py * W + px] = p.c[v];
            }
    }
}
static void text_c(int y, const char *s, pal_t p) { text(W / 2 - (int)strlen(s) * 4, y, s, p); }
static void text_r(int x, int y, const char *s, pal_t p) { text(x - (int)strlen(s) * 8, y, s, p); }

static void draw_box(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t p = ui[y * W + x];
            uint32_t r = ((p >> 16) & 255) / 4 + 4;
            uint32_t g = ((p >> 8) & 255) / 4 + 6;
            uint32_t b = (p & 255) / 4 + 18;
            ui[y * W + x] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    // Authentic Capcom silver-blue beveled border
    for (int x = x0; x < x1; x++) {
        ui[y0 * W + x] = 0xffd0e0f8u; ui[(y0 + 1) * W + x] = 0xff506898u;
        ui[(y1 - 1) * W + x] = 0xff506898u; ui[(y1 - 2) * W + x] = 0xff98b0d8u;
    }
    for (int y = y0; y < y1; y++) {
        ui[y * W + x0] = 0xffd0e0f8u; ui[y * W + x0 + 1] = 0xff506898u;
        ui[y * W + x1 - 1] = 0xff506898u; ui[y * W + x1 - 2] = 0xff98b0d8u;
    }
}

static void draw_cursor(int x, int y)
{
    for (int yy = 0; yy < curh; yy++)
        for (int xx = 0; xx < curw; xx++) {
            int px = x + xx, py = y + yy;
            if (cursor[yy * curw + xx] && px >= 0 && px < W && py >= 0 && py < H)
                ui[py * W + px] = cursor[yy * curw + xx];
        }
}

/* ------------------------------------------------------------------ */
/* Settings, Roster Reload, Live Match Modifiers                        */
/* ------------------------------------------------------------------ */

static const int snes_btn[12] = {
    RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT,
    RETRO_DEVICE_ID_JOYPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A,
    RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_L,
    RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT
};
static const char *snes_name[12]    = { "ARRIBA", "ABAJO", "IZQ", "DER", "B TIRO", "A LOB", "Y PASE", "X TURBO", "L", "R", "START", "SELECT" };
static const char *snes_name_en[12] = { "UP", "DOWN", "LEFT", "RIGHT", "B SHOOT", "A LOB", "Y PASS", "X DASH", "L", "R", "START", "SELECT" };
static const int kb_default[12] = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_A, SDL_SCANCODE_S,
    SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT
};
static const int gp_default[12] = {
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK
};

static struct {
    int lang, fullscreen, touch;      // touch: 0=auto, 1=on, 2=off
    int roster;                       // 0=Original 1994, 1=Actual 2026, 2=Leyendas 1994
    int freeze_clock, golden_goal, turbo, fastcpu, hud_banner;
    int kb[12], gp[12];
    char rom_path[1024];
} cfg = { -1, .roster = 1, .hud_banner = 1 };

#define T(es, en) (cfg.lang == 1 ? (en) : (es))

static void cfg_path(char *out, size_t n) { snprintf(out, n, "%ssoccer.cfg", save_dir); }

static void cfg_load(void)
{
    char p[1100], line[1200], k[64];
    int v, n;
    cfg_path(p, sizeof p);
    memcpy(cfg.kb, kb_default, sizeof cfg.kb);
    memcpy(cfg.gp, gp_default, sizeof cfg.gp);
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!strncmp(line, "rom_path=", 9)) { snprintf(cfg.rom_path, sizeof cfg.rom_path, "%s", line + 9); continue; }
        if (sscanf(line, " %63[^=]=%d", k, &v) != 2) continue;
        if (!strcmp(k, "lang")) cfg.lang = v;
        else if (!strcmp(k, "fullscreen")) cfg.fullscreen = v;
        else if (!strcmp(k, "touch_controls")) cfg.touch = (v >= 0 && v <= 2) ? v : 0;
        else if (!strcmp(k, "roster_pack")) cfg.roster = (v >= 0 && v <= 2) ? v : 1;
        else if (!strcmp(k, "freeze_clock")) cfg.freeze_clock = v;
        else if (!strcmp(k, "golden_goal")) cfg.golden_goal = v;
        else if (!strcmp(k, "turbo")) cfg.turbo = v;
        else if (!strcmp(k, "no_slowdown")) cfg.fastcpu = v;
        else if (!strcmp(k, "hud_banner")) cfg.hud_banner = v;
        else if (sscanf(k, "key_%d", &n) == 1 && n >= 0 && n < 12) cfg.kb[n] = v;
        else if (sscanf(k, "pad_%d", &n) == 1 && n >= 0 && n < 12) cfg.gp[n] = v;
    }
    fclose(f);
}

static void cfg_save(void)
{
    char p[1100]; cfg_path(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return;
    if (cfg.rom_path[0]) fprintf(f, "rom_path=%s\n", cfg.rom_path);
    fprintf(f, "lang=%d\nfullscreen=%d\ntouch_controls=%d\nroster_pack=%d\n"
               "freeze_clock=%d\ngolden_goal=%d\nturbo=%d\nno_slowdown=%d\nhud_banner=%d\n",
            cfg.lang, cfg.fullscreen, cfg.touch, cfg.roster,
            cfg.freeze_clock, cfg.golden_goal, cfg.turbo, cfg.fastcpu, cfg.hud_banner);
    for (int i = 0; i < 12; i++) fprintf(f, "key_%d=%d\npad_%d=%d\n", i, cfg.kb[i], i, cfg.gp[i]);
    fclose(f);
}

static void guess_language(void)
{
    if (cfg.lang >= 0) return;
    cfg.lang = 1;
    SDL_Locale *l = SDL_GetPreferredLocales();
    if (l && l[0].language && !strcmp(l[0].language, "es")) cfg.lang = 0;
    if (l) SDL_free(l);
}

static const char *roster_label(int pack)
{
    if (pack == 1) return T("ACTUAL 2026", "MODERN 2026");
    if (pack == 2) return T("LEYENDAS 94", "LEGENDS 94");
    return T("ORIGINAL 94", "ORIGINAL 94");
}

static bool reload_rom_with_roster(int pack)
{
    if (!rom_work) return false;
    apply_roster_pack(rom_work, orig_roster, orig_roster_t2, pack);
    if (core.unload_game) core.unload_game();
    struct retro_game_info gi = { "soccer.sfc", rom_work, ROM_SIZE, NULL };
    if (!core.load_game(&gi)) return false;
    // Rebuild mode_snap on the newly loaded roster buffer so quick launches carry the new names
    mute = true; scripting = true; layers_dirty = true;
    free(mode_snap); mode_snap = NULL;
    boot_to_mode_select();
    mode_snap_size = core.ser_size();
    mode_snap = malloc(mode_snap_size);
    if (mode_snap && !core.serialize(mode_snap, mode_snap_size)) { free(mode_snap); mode_snap = NULL; }
    scripting = false; mute = false; layers_dirty = true;
    return true;
}

// Verified WRAM (7E:xxxx) addresses for Capcom's Soccer Shootout (USA)
enum {
    RAM_P1_SCORE = 0x06CE,
    RAM_P2_SCORE = 0x06CF,
    RAM_CLOCK_S  = 0x18A2,
    RAM_CLOCK_M  = 0x18A3
};

static bool started;
static uint8_t frozen_m, frozen_s;
static bool clock_captured;
static uint8_t gg_p1, gg_p2;
static bool gg_armed;
static uint32_t turbo_tick;
static void say(const char *s);

static void match_hooks(void)
{
    uint8_t *m = wram();
    if (!m || !started) return;
    bool in_match = (m[RAM_CLOCK_M] <= 15 && (m[RAM_CLOCK_M] > 0 || m[RAM_CLOCK_S] > 0));

    if (cfg.freeze_clock && in_match) {
        if (!clock_captured) { frozen_m = m[RAM_CLOCK_M]; frozen_s = m[RAM_CLOCK_S]; clock_captured = true; }
        m[RAM_CLOCK_M] = frozen_m;
        m[RAM_CLOCK_S] = frozen_s;
    } else {
        clock_captured = false;
    }

    if (cfg.golden_goal && in_match) {
        if (!gg_armed) { gg_p1 = m[RAM_P1_SCORE]; gg_p2 = m[RAM_P2_SCORE]; gg_armed = true; }
        else if (m[RAM_P1_SCORE] > gg_p1 || m[RAM_P2_SCORE] > gg_p2) {
            bool p1_win = m[RAM_P1_SCORE] > gg_p1;
            m[RAM_CLOCK_M] = 0; m[RAM_CLOCK_S] = 0; // trigger immediate full-time whistle!
            gg_armed = false;
            say(p1_win ? T("GOL DE ORO! GANA P1!", "GOLDEN GOAL! P1 WINS!")
                       : T("GOL DE ORO! GANA P2!", "GOLDEN GOAL! P2 WINS!"));
        }
    } else {
        gg_armed = false;
    }
}

/* ------------------------------------------------------------------ */
/* Quick-Launch Mode Scripts (drives -MODE SELECT- 3x2 grid)            */
/* ------------------------------------------------------------------ */

static void set_scripting(bool on) { scripting = on; mute = on; layers_dirty = true; }

// mode_idx: 0=Exhibition, 1=Main Game, 2=All-Stars, 3=PK, 4=Indoor Soccer, 5=Training
static void script_launch_mode(int mode_idx)
{
    set_scripting(true);
    boot_to_mode_select();
    int col = mode_idx % 3, row = mode_idx / 3;
    for (int i = 0; i < col; i++) tap(RETRO_DEVICE_ID_JOYPAD_RIGHT, 14);
    for (int i = 0; i < row; i++) tap(RETRO_DEVICE_ID_JOYPAD_DOWN, 14);
    tap(RETRO_DEVICE_ID_JOYPAD_A, 70);
    set_scripting(false);
    started = true;
    clock_captured = false;
    gg_armed = false;
}

/* ------------------------------------------------------------------ */
/* Menus                                                                */
/* ------------------------------------------------------------------ */

enum { ST_START, ST_PLAY, ST_PAUSE } state = ST_START;
enum { P_START, P_PAUSE, P_MODS, P_ROSTERS, P_OPTIONS, P_CONTROLS } page = P_START;

static int sel, from_page;
static int view_team = 3; // default roster viewer to Argentina (or cycle all 12!)
static int view_page = 0; // 0 = starters 1..11, 1 = bench/reserves 12..20
static bool quit;
static int binding = -1;
static char toast[64];
static Uint32 toast_until;
static SDL_Window *win;

static void say(const char *s) { snprintf(toast, sizeof toast, "%s", s); toast_until = SDL_GetTicks() + 2000; }

static void state_path(char *out, size_t n) { snprintf(out, n, "%sslot1.state", save_dir); }
static void save_state(void)
{
    size_t n = core.ser_size();
    void *b = malloc(n);
    char p[1100]; state_path(p, sizeof p);
    FILE *f = (b && core.serialize(b, n)) ? fopen(p, "wb") : NULL;
    if (f) { fwrite(b, 1, n, f); fclose(f); say(T("ESTADO GUARDADO", "STATE SAVED")); }
    else say(T("NO SE PUDO GUARDAR", "COULD NOT SAVE"));
    free(b);
}
static void load_state(void)
{
    size_t n = core.ser_size();
    void *b = malloc(n);
    char p[1100]; state_path(p, sizeof p);
    FILE *f = fopen(p, "rb");
    bool ok = f && b && fread(b, 1, n, f) == n && core.unserialize(b, n);
    if (f) fclose(f);
    free(b);
    say(ok ? T("ESTADO CARGADO", "STATE LOADED") : T("NO HAY ESTADO GUARDADO", "NO SAVED STATE"));
    if (ok) { started = true; clock_captured = false; gg_armed = false; }
}

static void set_fullscreen(void) { if (win) SDL_SetWindowFullscreen(win, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
static void open_page(int p) { if (p != P_MODS && p != P_CONTROLS) from_page = page; page = p; sel = 0; }
static void resume(void) { state = ST_PLAY; if (audio) SDL_ClearQueuedAudio(audio); }
static void pause_game(void) { if (state == ST_PLAY) { state = ST_PAUSE; page = P_PAUSE; sel = 0; } }

typedef struct { const char *label, *value, *value2; } item_t;

static const char *key_name(int sc)
{
    static char b[12][10];
    static int k;
    const char *n = SDL_GetScancodeName((SDL_Scancode)sc);
    char *o = b[k++ % 12];
    snprintf(o, 9, "%s", (n && *n) ? n : "-");
    if (n && !strncmp(n, "Right ", 6)) snprintf(o, 9, "R%s", n + 6);
    else if (n && !strncmp(n, "Left ", 5)) snprintf(o, 9, "L%s", n + 5);
    return o;
}
static const char *pad_name(int b)
{
    static const char *n[] = { "A", "B", "X", "Y", "SELECT", "GUIDE", "START", "L3", "R3", "LB", "RB",
                               "ARRIBA", "ABAJO", "IZQ", "DER" };
    static const char *e[] = { "A", "B", "X", "Y", "SELECT", "GUIDE", "START", "L3", "R3", "LB", "RB",
                               "UP", "DOWN", "LEFT", "RIGHT" };
    if (b < 0 || b > 14) return "-";
    return cfg.lang == 1 ? e[b] : n[b];
}

static int build(item_t *it)
{
    int n = 0;
#define ADD(l, v) (it[n].label = (l), it[n].value = (v), it[n].value2 = NULL, n++)
    const char *yes = T("SI", "ON"), *no = T("NO", "OFF");
    switch (page) {
    case P_START:
        if (started) ADD(T("CONTINUAR", "CONTINUE"), NULL);
        ADD(T("PLANTILLAS", "ROSTERS"), roster_label(cfg.roster));
        ADD(T("EXHIBICION", "EXHIBITION"), NULL);
        ADD(T("TEMPORADA", "MAIN GAME"), NULL);
        ADD("ALL-STARS", NULL);
        ADD(T("PENALES (PK)", "PENALTY KICKS"), NULL);
        ADD("INDOOR 8V8", NULL);
        ADD(T("ENTRENAMIENTO", "TRAINING"), NULL);
        ADD(T("VER PLANTILLAS", "VIEW SQUADS"), NULL);
        ADD(T("CARGAR ESTADO", "LOAD STATE"), NULL);
        ADD(T("OPCIONES", "OPTIONS"), NULL);
        ADD(T("SALIR", "QUIT"), NULL);
        break;
    case P_PAUSE:
        ADD(T("CONTINUAR", "CONTINUE"), NULL);
        ADD(T("MODIFICADORES", "MATCH MODS"), NULL);
        ADD(T("VER PLANTILLAS", "VIEW SQUADS"), NULL);
        ADD(T("GUARDAR ESTADO", "SAVE STATE"), NULL);
        ADD(T("CARGAR ESTADO", "LOAD STATE"), NULL);
        ADD(T("OPCIONES", "OPTIONS"), NULL);
        ADD(T("MENU DE INICIO", "START MENU"), NULL);
        ADD(T("SALIR", "QUIT"), NULL);
        break;
    case P_MODS:
        ADD(T("RELOJ CONGELADO", "FREEZE CLOCK"), cfg.freeze_clock ? yes : no);
        ADD(T("GOL DE ORO", "GOLDEN GOAL"), cfg.golden_goal ? yes : no);
        ADD(T("VELOCIDAD TURBO", "TURBO SPEED"), cfg.turbo ? yes : no);
        ADD(T("SIN SLOWDOWN", "NO SLOWDOWN"), cfg.fastcpu ? yes : no);
        ADD(T("+1 GOL P1", "+1 GOAL P1"), NULL);
        ADD(T("+1 GOL P2", "+1 GOAL P2"), NULL);
        ADD(T("MARCADOR 0-0", "RESET SCORE"), NULL);
        ADD(T("+1 MINUTO", "+1 MINUTE"), NULL);
        ADD(T("VOLVER", "BACK"), NULL);
        break;
    case P_ROSTERS:
        ADD(T("VOLVER", "BACK"), NULL);
        break;
    case P_OPTIONS: {
        const char *ts = cfg.touch == 0 ? "AUTO" : (cfg.touch == 1 ? yes : no);
        ADD(T("PLANTILLAS", "ROSTERS"), roster_label(cfg.roster));
        ADD(T("IDIOMA", "LANGUAGE"), cfg.lang == 1 ? "ENGLISH" : "ESPANOL");
        ADD(T("PANTALLA COMP", "FULL SCREEN"), cfg.fullscreen ? yes : no);
        ADD(T("ETIQUETA PACK", "PACK BADGE"), cfg.hud_banner ? yes : no);
        ADD(T("TACTIL", "TOUCH PAD"), ts);
        ADD(T("CONTROLES", "CONTROLS"), NULL);
        ADD(T("VOLVER", "BACK"), NULL);
        break;
    }
    case P_CONTROLS:
        for (int i = 0; i < 12; i++) {
            ADD(cfg.lang == 1 ? snes_name_en[i] : snes_name[i], binding == i ? "..." : key_name(cfg.kb[i]));
            it[n - 1].value2 = binding == i ? "..." : pad_name(cfg.gp[i]);
        }
        ADD(T("RESTAURAR", "DEFAULTS"), NULL);
        ADD(T("VOLVER", "BACK"), NULL);
        break;
    }
    return n;
#undef ADD
}

static void back(void)
{
    binding = -1;
    if (page == P_PAUSE) resume();
    else if (page == P_MODS) { page = P_PAUSE; sel = 1; }
    else if (page == P_CONTROLS) { page = P_OPTIONS; sel = 5; }
    else { page = from_page; sel = 0; }
}

static void cycle_roster_pack(int delta)
{
    cfg.roster = (cfg.roster + delta + 3) % 3;
    cfg_save();
    reload_rom_with_roster(cfg.roster);
    started = false;
    say(roster_label(cfg.roster));
}

static void activate(void)
{
    item_t it[24];
    int n = build(it);
    if (sel >= n) return;
    const char *l = it[sel].label;
    switch (page) {
    case P_START:
        if (!strcmp(l, T("CONTINUAR", "CONTINUE"))) resume();
        else if (!strcmp(l, T("PLANTILLAS", "ROSTERS"))) cycle_roster_pack(1);
        else if (!strcmp(l, T("EXHIBICION", "EXHIBITION"))) { script_launch_mode(0); resume(); }
        else if (!strcmp(l, T("TEMPORADA", "MAIN GAME"))) { script_launch_mode(1); resume(); }
        else if (!strcmp(l, "ALL-STARS")) { script_launch_mode(2); resume(); }
        else if (!strcmp(l, T("PENALES (PK)", "PENALTY KICKS"))) { script_launch_mode(3); resume(); }
        else if (!strcmp(l, "INDOOR 8V8")) { script_launch_mode(4); resume(); }
        else if (!strcmp(l, T("ENTRENAMIENTO", "TRAINING"))) { script_launch_mode(5); resume(); }
        else if (!strcmp(l, T("VER PLANTILLAS", "VIEW SQUADS"))) { view_page = 0; open_page(P_ROSTERS); }
        else if (!strcmp(l, T("CARGAR ESTADO", "LOAD STATE"))) { load_state(); if (started) resume(); }
        else if (!strcmp(l, T("OPCIONES", "OPTIONS"))) open_page(P_OPTIONS);
        else quit = true;
        break;
    case P_PAUSE:
        if (!strcmp(l, T("CONTINUAR", "CONTINUE"))) resume();
        else if (!strcmp(l, T("MODIFICADORES", "MATCH MODS"))) open_page(P_MODS);
        else if (!strcmp(l, T("VER PLANTILLAS", "VIEW SQUADS"))) { view_page = 0; open_page(P_ROSTERS); }
        else if (!strcmp(l, T("GUARDAR ESTADO", "SAVE STATE"))) save_state();
        else if (!strcmp(l, T("CARGAR ESTADO", "LOAD STATE"))) load_state();
        else if (!strcmp(l, T("OPCIONES", "OPTIONS"))) open_page(P_OPTIONS);
        else if (!strcmp(l, T("MENU DE INICIO", "START MENU"))) { state = ST_START; page = P_START; sel = 0; }
        else quit = true;
        break;
    case P_MODS: {
        uint8_t *m = wram();
        if (sel == 0) { cfg.freeze_clock ^= 1; clock_captured = false; }
        else if (sel == 1) { cfg.golden_goal ^= 1; gg_armed = false; }
        else if (sel == 2) cfg.turbo ^= 1;
        else if (sel == 3) { cfg.fastcpu ^= 1; overclock = cfg.fastcpu; layers_dirty = true; }
        else if (sel == 4 && m) { if (m[RAM_P1_SCORE] < 99) m[RAM_P1_SCORE]++; say(T("+1 GOL P1", "+1 GOAL P1")); }
        else if (sel == 5 && m) { if (m[RAM_P2_SCORE] < 99) m[RAM_P2_SCORE]++; say(T("+1 GOL P2", "+1 GOAL P2")); }
        else if (sel == 6 && m) { m[RAM_P1_SCORE] = 0; m[RAM_P2_SCORE] = 0; gg_armed = false; say("0 - 0"); }
        else if (sel == 7 && m) { if (m[RAM_CLOCK_M] < 15) m[RAM_CLOCK_M]++; clock_captured = false; say(T("+1 MINUTO", "+1 MINUTE")); }
        else back();
        cfg_save();
        break;
    }
    case P_ROSTERS:
        back();
        break;
    case P_OPTIONS:
        if (sel == 0) cycle_roster_pack(1);
        else if (sel == 1) cfg.lang ^= 1;
        else if (sel == 2) { cfg.fullscreen ^= 1; set_fullscreen(); }
        else if (sel == 3) cfg.hud_banner ^= 1;
        else if (sel == 4) cfg.touch = (cfg.touch + 1) % 3;
        else if (sel == 5) { page = P_CONTROLS; sel = 0; }
        else back();
        cfg_save();
        break;
    case P_CONTROLS:
        if (sel < 12) { binding = sel; say(T("PULSA TECLA O BOTON", "PRESS KEY OR BUTTON")); }
        else if (sel == 12) {
            memcpy(cfg.kb, kb_default, sizeof cfg.kb); memcpy(cfg.gp, gp_default, sizeof cfg.gp);
            cfg_save(); say(T("CONTROLES RESTAURADOS", "CONTROLS RESET"));
        } else back();
        break;
    }
}

static int menu_last_y0, menu_last_step, menu_last_first, menu_last_rows, menu_last_n;

static void draw_menu(void)
{
    memcpy(ui, bg, sizeof ui);
    draw_box(12, 30, 244, 210);

    static const char *title_es[] = { "SOCCER SHOOTOUT", "PAUSA", "MODIFICADORES", "PLANTILLAS", "OPCIONES", "CONTROLES" };
    static const char *title_en[] = { "SOCCER SHOOTOUT", "PAUSED", "MATCH MODS", "SQUAD ROSTERS", "OPTIONS", "CONTROLS" };
    const char *title = cfg.lang == 1 ? title_en[page] : title_es[page];
    // Top title banner
    int tw = (int)strlen(title) * 8 + 16;
    for (int y = 10; y < 24; y++)
        for (int x = W / 2 - tw / 2; x < W / 2 + tw / 2; x++)
            if (x >= 0 && x < W) ui[y * W + x] = 0xe8080c24u;
    text_c(13, title, GOLD);

    item_t it[24];
    int n = build(it);
    int x = 32, step = 13, y0 = 40, vx = 234;
    int first = 0, rows = n;
    if (page == P_START)    { x = 30; step = 12; y0 = 38; rows = 11; }
    if (page == P_PAUSE)    { x = 36; step = 16; y0 = 46; }
    if (page == P_MODS)     { x = 28; step = 16; y0 = 42; }
    if (page == P_OPTIONS)  { x = 28; step = 18; y0 = 46; }
    if (page == P_CONTROLS) { x = 24; step = 10; y0 = 52; vx = 162; }

    if (n > rows) {
        first = sel - rows / 2;
        if (first > n - rows) first = n - rows;
        if (first < 0) first = 0;
    }

    if (page == P_ROSTERS) {
        char hdr[40];
        snprintf(hdr, sizeof hdr, "< %s >", roster_team_names[view_team]);
        text_c(38, hdr, GOLD);
        text_c(49, roster_label(cfg.roster), CYAN);
        // Show 11 starters (view_page==0) or 9 subs/reserves (view_page==1) in 2 columns!
        int start_p = view_page ? 11 : 0;
        int end_p   = view_page ? 20 : 11;
        for (int p = start_p; p < end_p; p++) {
            int row = (p - start_p) % 6;
            int col = (p - start_p) / 6;
            char line[24];
            const char *pname = NULL;
            char orig_buf[16];
            if (cfg.roster == 1) pname = rosters_2026[view_team][p];
            else if (cfg.roster == 2) pname = rosters_1994[view_team][p];
            else {
                const uint8_t *ob = orig_roster + (view_team * ROSTER_PLAYERS + p) * ROSTER_NAME_LEN;
                int k = 0;
                for (int c = 0; c < ROSTER_NAME_LEN; c++) {
                    if (ob[c] <= 25) orig_buf[k++] = (char)('A' + ob[c]);
                    else if (ob[c] >= 0x20 && ob[c] <= 0x39) orig_buf[k++] = (char)('a' + ob[c] - 0x20);
                    else if (k > 0 && orig_buf[k - 1] != ' ') orig_buf[k++] = ' ';
                }
                while (k > 0 && orig_buf[k - 1] == ' ') k--;
                orig_buf[k] = 0;
                pname = orig_buf;
            }
            snprintf(line, sizeof line, "%2d %.10s", p + 1, pname);
            int rx = col == 0 ? 20 : 134;
            int ry = 64 + row * 15;
            pal_t cp = (p == 0 || p == 15) ? GREEN : (p < 11 ? WHITE : DIM);
            text(rx, ry, line, cp);
        }
        text_c(162, T("IZQ/DER:EQUIPO ARR/ABJ:BANCA", "L/R:TEAM  UP/DN:BENCH"), DIM);
        y0 = 182; x = 36;
    }

    if (page == P_CONTROLS) {
        text_r(162, 38, T("TECLADO", "KEYS"), DIM);
        text(172, 38, T("MANDO", "PAD"), DIM);
    }

    menu_last_y0 = y0; menu_last_step = step; menu_last_first = first; menu_last_rows = rows; menu_last_n = n;

    for (int i = first; i < n && i < first + rows; i++) {
        int y = y0 + (i - first) * step;
        bool on = (i == sel);
        bool plain = (page == P_START || page == P_PAUSE);
        text(x, y, it[i].label, on ? GOLD : (plain ? DIM : WHITE));
        if (it[i].value) text_r(vx, y, it[i].value, on ? GREEN : CYAN);
        if (it[i].value2) text(172, y, it[i].value2, on ? GOLD : WHITE);
        if (on) draw_cursor(x - curw - 3, y - (curh - 8) / 2);
    }

    const char *foot = "";
    if (page == P_START)     foot = T("F1/ESC O BACK: PAUSA", "F1/ESC OR BACK: PAUSE");
    else if (page == P_MODS) foot = T("SE APLICA EN VIVO", "APPLIES LIVE IN MATCH");
    if (*foot) text_c(195, foot, DIM);
}

static void draw_toast(void)
{
    if (!*toast || SDL_TICKS_PASSED(SDL_GetTicks(), toast_until)) { *toast = 0; return; }
    int w = (int)strlen(toast) * 8 + 10;
    for (int y = 4; y < 18; y++)
        for (int x = W / 2 - w / 2; x < W / 2 + w / 2; x++)
            if (x >= 0 && x < W) ui[y * W + x] = 0xe8080c24u;
    text_c(7, toast, GOLD);
}

/* ------------------------------------------------------------------ */
/* Input, 2P Support, Gamepad & Multi-Touch Virtual Controls            */
/* ------------------------------------------------------------------ */

static SDL_GameController *pads[4];

#define MAX_FINGERS 10
typedef struct {
    SDL_FingerID id;
    float x, y, start_y;
    bool down, scrolled;
} finger_t;

static finger_t fingers[MAX_FINGERS];
#ifdef __ANDROID__
static bool touch_seen = true;
#else
static bool touch_seen = false;
#endif
static bool pad_active_recently = false;
static Uint32 last_pad_tick = 0;

typedef struct {
    float cx, cy, hw, hh;
    int btn;
    const char *label;
    uint8_t r, g, b;
} tbtn_t;

enum { TACT_MENU = -1, TACT_SAVE = -2, TACT_LOAD = -3 };

static const tbtn_t touch_btns[] = {
    // Soccer-friendly SNES Diamond: Y=PASS, B=SHOOT, A=LOB, X=DASH
    { 0.805f, 0.675f, 0.048f, 0.082f, RETRO_DEVICE_ID_JOYPAD_Y,      "PASS",  55, 210,  95 },
    { 0.865f, 0.785f, 0.048f, 0.082f, RETRO_DEVICE_ID_JOYPAD_B,      "SHOT", 240, 195,  45 },
    { 0.925f, 0.675f, 0.048f, 0.082f, RETRO_DEVICE_ID_JOYPAD_A,      "LOB",  230,  70,  70 },
    { 0.865f, 0.565f, 0.048f, 0.082f, RETRO_DEVICE_ID_JOYPAD_X,      "DASH",  70, 155, 245 },
    { 0.085f, 0.365f, 0.062f, 0.052f, RETRO_DEVICE_ID_JOYPAD_L,      "L",    180, 195, 220 },
    { 0.915f, 0.365f, 0.062f, 0.052f, RETRO_DEVICE_ID_JOYPAD_R,      "R",    180, 195, 220 },
    { 0.145f, 0.925f, 0.058f, 0.042f, RETRO_DEVICE_ID_JOYPAD_SELECT, "SEL",  150, 165, 190 },
    { 0.855f, 0.925f, 0.058f, 0.042f, RETRO_DEVICE_ID_JOYPAD_START,  "STRT", 150, 165, 190 },
    { 0.080f, 0.068f, 0.065f, 0.048f, TACT_MENU,                     "MENU", 245, 205,  60 },
    { 0.840f, 0.068f, 0.052f, 0.044f, TACT_SAVE,                     "SAVE",  80, 210, 245 },
    { 0.942f, 0.068f, 0.052f, 0.044f, TACT_LOAD,                     "LOAD", 120, 230, 140 },
};
#define NTOUCH_BTNS (int)(sizeof touch_btns / sizeof touch_btns[0])
static const float DPAD_CX = 0.125f, DPAD_CY = 0.695f, DPAD_RX = 0.095f, DPAD_RY = 0.165f;

static bool touch_pad_visible(void)
{
    if (cfg.touch == 1) return true;
    if (cfg.touch == 2) return false;
    if (!touch_seen) return false;
    if (pad_active_recently && (SDL_GetTicks() - last_pad_tick) < 15000u) return false;
    return true;
}
static bool touch_menu_pill_visible(void)
{
    if (cfg.touch == 2) return false;
    return touch_seen || cfg.touch == 1;
}

static uint16_t read_touch_pad(void)
{
    if (!touch_pad_visible()) return 0;
    uint16_t b = 0;
    for (int f = 0; f < MAX_FINGERS; f++) {
        if (!fingers[f].down) continue;
        float fx = fingers[f].x, fy = fingers[f].y;
        float dx = (fx - DPAD_CX) / DPAD_RX, dy = (fy - DPAD_CY) / DPAD_RY;
        float d2 = dx * dx + dy * dy;
        if (d2 <= 1.45f && d2 >= 0.04f) {
            if (dx < -0.32f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
            if (dx >  0.32f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
            if (dy < -0.32f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
            if (dy >  0.32f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
        }
        for (int i = 0; i < NTOUCH_BTNS; i++) {
            const tbtn_t *tb = &touch_btns[i];
            if (tb->btn < 0) continue;
            if (fx >= tb->cx - tb->hw && fx <= tb->cx + tb->hw &&
                fy >= tb->cy - tb->hh && fy <= tb->cy + tb->hh) b |= (uint16_t)(1u << tb->btn);
        }
    }
    return b;
}

static uint16_t read_controller_bits(SDL_GameController *c)
{
    if (!c) return 0;
    uint16_t pb = 0;
    for (int i = 0; i < 12; i++)
        if (cfg.gp[i] >= 0 && SDL_GameControllerGetButton(c, (SDL_GameControllerButton)cfg.gp[i]))
            pb |= (uint16_t)(1u << snes_btn[i]);
    Sint16 ax = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    Sint16 ay = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    if (ax < -16000) pb |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (ax > 16000)  pb |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
    if (ay < -16000) pb |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
    if (ay > 16000)  pb |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (pb) { pad_active_recently = true; last_pad_tick = SDL_GetTicks(); }
    return pb;
}

static void read_both_pads(uint16_t *p1, uint16_t *p2)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint16_t b1 = read_touch_pad(), b2 = 0;
    for (int i = 0; i < 12; i++)
        if (cfg.kb[i] > 0 && cfg.kb[i] < SDL_NUM_SCANCODES && k[cfg.kb[i]]) b1 |= (uint16_t)(1u << snes_btn[i]);
    // Secondary keyboard layout for P2 (IJKL + U/O/H/N + M=Start)
    if (k[SDL_SCANCODE_I]) b2 |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
    if (k[SDL_SCANCODE_K]) b2 |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (k[SDL_SCANCODE_J]) b2 |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (k[SDL_SCANCODE_L]) b2 |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
    if (k[SDL_SCANCODE_N]) b2 |= 1u << RETRO_DEVICE_ID_JOYPAD_B;
    if (k[SDL_SCANCODE_M]) b2 |= 1u << RETRO_DEVICE_ID_JOYPAD_A;
    if (k[SDL_SCANCODE_H]) b2 |= 1u << RETRO_DEVICE_ID_JOYPAD_Y;
    if (k[SDL_SCANCODE_U]) b2 |= 1u << RETRO_DEVICE_ID_JOYPAD_X;
    if (k[SDL_SCANCODE_O]) b2 |= 1u << RETRO_DEVICE_ID_JOYPAD_START;

    b1 |= read_controller_bits(pads[0]);
    b2 |= read_controller_bits(pads[1]);
    *p1 = b1; *p2 = b2;
}

enum { M_NONE, M_UP, M_DOWN, M_LEFT, M_RIGHT, M_OK, M_BACK, M_PAUSE, M_PGUP, M_PGDN };

static void menu_input(int m)
{
    if (page == P_ROSTERS) {
        if (m == M_LEFT)       { view_team = (view_team + ROSTER_TEAMS - 1) % ROSTER_TEAMS; return; }
        else if (m == M_RIGHT) { view_team = (view_team + 1) % ROSTER_TEAMS; return; }
        else if (m == M_UP || m == M_DOWN) { view_page ^= 1; return; }
        else if (m == M_BACK || m == M_OK) { back(); return; }
        return;
    }
    item_t it[24];
    int n = build(it);
    if (n <= 0) return;
    if (m == M_UP) sel = (sel + n - 1) % n;
    else if (m == M_DOWN) sel = (sel + 1) % n;
    else if (m == M_PGUP) { sel -= 5; if (sel < 0) sel = 0; }
    else if (m == M_PGDN) { sel += 5; if (sel >= n) sel = n - 1; }
    else if (m == M_LEFT && it[sel].value && !strcmp(it[sel].label, T("PLANTILLAS", "ROSTERS"))) cycle_roster_pack(-1);
    else if (m == M_OK || ((m == M_LEFT || m == M_RIGHT) && it[sel].value && page != P_CONTROLS)) activate();
    else if (m == M_BACK) back();
}

static int key_action(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: return M_UP;
    case SDLK_DOWN: return M_DOWN;
    case SDLK_LEFT: return M_LEFT;
    case SDLK_RIGHT: return M_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_x: case SDLK_z: case SDLK_SPACE: return M_OK;
    case SDLK_BACKSPACE: return M_BACK;
    case SDLK_ESCAPE: case SDLK_F1: case SDLK_AC_BACK: return M_PAUSE;
    }
    return M_NONE;
}

static int pad_action(int b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return M_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return M_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return M_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return M_RIGHT;
    case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: return M_OK;
    case SDL_CONTROLLER_BUTTON_B: return M_BACK;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return M_PGUP;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return M_PGDN;
    case SDL_CONTROLLER_BUTTON_BACK: case SDL_CONTROLLER_BUTTON_GUIDE: return M_PAUSE;
    }
    return M_NONE;
}

static void handle(int m)
{
    if (m == M_NONE) return;
    if (state == ST_PLAY) { if (m == M_PAUSE) pause_game(); return; }
    if (m == M_PAUSE) { if (page == P_START) return; back(); return; }
    menu_input(m);
}

static void poll_stick_menu(void)
{
    static int held_dir = M_NONE;
    static Uint32 next_rep = 0;
    if (state == ST_PLAY || binding >= 0) { held_dir = M_NONE; return; }
    int dir = M_NONE;
    for (int p = 0; p < 4; p++) {
        SDL_GameController *c = pads[p];
        if (!c) continue;
        Sint16 ax = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ay = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        if (ay < -18000) { dir = M_UP; break; }
        if (ay >  18000) { dir = M_DOWN; break; }
        if (ax < -18000) { dir = M_LEFT; break; }
        if (ax >  18000) { dir = M_RIGHT; break; }
    }
    Uint32 now = SDL_GetTicks();
    if (dir == M_NONE) { held_dir = M_NONE; return; }
    pad_active_recently = true; last_pad_tick = now;
    if (dir != held_dir) { held_dir = dir; handle(dir); next_rep = now + 320; }
    else if (SDL_TICKS_PASSED(now, next_rep)) { if (dir == M_UP || dir == M_DOWN) handle(dir); next_rep = now + 130; }
}

static void bind_key(int sc)
{
    if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_F1) { binding = -1; *toast = 0; return; }
    for (int i = 0; i < 12; i++) if (cfg.kb[i] == sc) cfg.kb[i] = cfg.kb[binding];
    cfg.kb[binding] = sc; binding = -1; *toast = 0; cfg_save();
}
static void bind_pad(int b)
{
    if (b == SDL_CONTROLLER_BUTTON_GUIDE) { binding = -1; *toast = 0; return; }
    for (int i = 0; i < 12; i++) if (cfg.gp[i] == b) cfg.gp[i] = cfg.gp[binding];
    cfg.gp[binding] = b; binding = -1; *toast = 0; cfg_save();
}

static SDL_Rect compute_game_rect(int rw, int rh)
{
    SDL_Rect r;
    if (rw <= 0 || rh <= 0) { r.x = 0; r.y = 0; r.w = 1024; r.h = 768; return r; }
    int gh_fit = rh, gw_fit = rh * 4 / 3;
    if (gw_fit > rw) { gw_fit = rw; gh_fit = rw * 3 / 4; }
    r.x = (rw - gw_fit) / 2; r.y = (rh - gh_fit) / 2; r.w = gw_fit; r.h = gh_fit;
    return r;
}

static void handle_pointer_tap(float fx, float fy, int rw, int rh)
{
    if (state == ST_PLAY) {
        for (int i = 0; i < NTOUCH_BTNS; i++) {
            const tbtn_t *tb = &touch_btns[i];
            if (tb->btn >= 0) continue;
            if (tb->btn != TACT_MENU && !touch_pad_visible()) continue;
            if (tb->btn == TACT_MENU && !touch_menu_pill_visible()) continue;
            if (fx >= tb->cx - tb->hw && fx <= tb->cx + tb->hw &&
                fy >= tb->cy - tb->hh && fy <= tb->cy + tb->hh) {
                if (tb->btn == TACT_MENU) pause_game();
                else if (tb->btn == TACT_SAVE) save_state();
                else if (tb->btn == TACT_LOAD) load_state();
                return;
            }
        }
        return;
    }

    if (page != P_START && ((fx <= 0.17f && fy <= 0.14f) || (fx <= 0.18f && fy >= 0.86f))) {
        back(); return;
    }
    if (page == P_ROSTERS) {
        if (fx < 0.35f) handle(M_LEFT);
        else if (fx > 0.65f) handle(M_RIGHT);
        else handle(M_UP);
        return;
    }

    SDL_Rect gr = compute_game_rect(rw, rh);
    float px = fx * (float)rw, py = fy * (float)rh;
    if (px < gr.x || px > gr.x + gr.w || py < gr.y || py > gr.y + gr.h) return;
    int uy = (int)((py - gr.y) * (float)H / (float)gr.h);

    for (int i = menu_last_first; i < menu_last_n && i < menu_last_first + menu_last_rows; i++) {
        int ry = menu_last_y0 + (i - menu_last_first) * menu_last_step;
        if (uy >= ry - 2 && uy < ry + menu_last_step - 1) { sel = i; activate(); return; }
    }
}

static void draw_mini_char(SDL_Renderer *ren, int x, int y, int sc, char ch)
{
    static const char *glyphs[36] = {
        "111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
        "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111",
        "010101111101101", "110101110101110", "111100100100111", "110101101101110", "111100110100111",
        "111100110100100", "111100101101111", "101101111101101", "111010010010111", "001001001101111",
        "101101110101101", "100100100100111", "101111101101101", "111101101101101", "111101101101111",
        "111101111100100", "111101101111001", "110101110101101", "111100111001111", "111010010010010",
        "101101101101111", "101101101101010", "101101101111101", "101101010101101", "101101010010010",
        "111001010100111"
    };
    int idx = (ch >= '0' && ch <= '9') ? (ch - '0') : ((ch >= 'A' && ch <= 'Z') ? (10 + ch - 'A') : ((ch >= 'a' && ch <= 'z') ? (10 + ch - 'a') : -1));
    if (idx < 0) return;
    const char *g = glyphs[idx];
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if (g[r * 3 + c] == '1') { SDL_Rect rc = { x + c * sc, y + r * sc, sc, sc }; SDL_RenderFillRect(ren, &rc); }
}

static void draw_mini_text(SDL_Renderer *ren, int cx, int cy, int sc, const char *s)
{
    int len = (int)strlen(s);
    int w = len * (4 * sc) - sc;
    int x = cx - w / 2, y = cy - (5 * sc) / 2;
    for (int i = 0; i < len; i++) draw_mini_char(ren, x + i * 4 * sc, y, sc, s[i]);
}

static void draw_touch_overlay(SDL_Renderer *ren, int rw, int rh, uint16_t held)
{
    if (!touch_menu_pill_visible()) return;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    int sc = rh / 220; if (sc < 2) sc = 2;

    if (state != ST_PLAY) {
        if (page != P_START) {
            SDL_Rect br = { (int)(0.025f * rw), (int)(0.88f * rh), (int)(0.145f * rw), (int)(0.09f * rh) };
            SDL_SetRenderDrawColor(ren, 16, 24, 48, 170); SDL_RenderFillRect(ren, &br);
            SDL_SetRenderDrawColor(ren, 245, 205, 60, 220); SDL_RenderDrawRect(ren, &br);
            SDL_SetRenderDrawColor(ren, 255, 240, 160, 240);
            draw_mini_text(ren, br.x + br.w / 2, br.y + br.h / 2, sc, T("VOLVER", "BACK"));
        }
        return;
    }

    bool full = touch_pad_visible();
    for (int i = 0; i < NTOUCH_BTNS; i++) {
        const tbtn_t *tb = &touch_btns[i];
        if (!full && tb->btn != TACT_MENU) continue;
        bool pressed = (tb->btn >= 0) && ((held >> tb->btn) & 1);
        SDL_Rect rc = { (int)((tb->cx - tb->hw) * rw), (int)((tb->cy - tb->hh) * rh),
                        (int)(tb->hw * 2.0f * rw), (int)(tb->hh * 2.0f * rh) };
        SDL_SetRenderDrawColor(ren, tb->r / 4, tb->g / 4, tb->b / 4, pressed ? 175 : (full ? 85 : 65));
        SDL_RenderFillRect(ren, &rc);
        SDL_SetRenderDrawColor(ren, tb->r, tb->g, tb->b, pressed ? 255 : (full ? 190 : 140));
        SDL_RenderDrawRect(ren, &rc);
        SDL_SetRenderDrawColor(ren, 255, 255, 255, pressed ? 255 : 210);
        draw_mini_text(ren, rc.x + rc.w / 2, rc.y + rc.h / 2, sc, tb->label);
    }

    if (!full) return;
    int dcx = (int)(DPAD_CX * rw), dcy = (int)(DPAD_CY * rh);
    int aw = (int)(DPAD_RX * 0.58f * rw), al = (int)(DPAD_RX * 1.15f * rw);
    int ah = (int)(DPAD_RY * 0.58f * rh), av = (int)(DPAD_RY * 1.15f * rh);
    SDL_Rect horiz = { dcx - al, dcy - ah / 2, al * 2, ah };
    SDL_Rect vert  = { dcx - aw / 2, dcy - av, aw, av * 2 };
    SDL_SetRenderDrawColor(ren, 20, 28, 48, 95);
    SDL_RenderFillRect(ren, &horiz); SDL_RenderFillRect(ren, &vert);
    SDL_SetRenderDrawColor(ren, 175, 195, 230, 180);
    SDL_RenderDrawRect(ren, &horiz); SDL_RenderDrawRect(ren, &vert);
    SDL_SetRenderDrawColor(ren, 250, 215, 70, 185);
    if (held & (1u << RETRO_DEVICE_ID_JOYPAD_UP))    { SDL_Rect r = { dcx - aw / 2 + 2, dcy - av + 2, aw - 4, av - ah / 2 }; SDL_RenderFillRect(ren, &r); }
    if (held & (1u << RETRO_DEVICE_ID_JOYPAD_DOWN))  { SDL_Rect r = { dcx - aw / 2 + 2, dcy + ah / 2 - 2, aw - 4, av - ah / 2 }; SDL_RenderFillRect(ren, &r); }
    if (held & (1u << RETRO_DEVICE_ID_JOYPAD_LEFT))  { SDL_Rect r = { dcx - al + 2, dcy - ah / 2 + 2, al - aw / 2, ah - 4 }; SDL_RenderFillRect(ren, &r); }
    if (held & (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT)) { SDL_Rect r = { dcx + aw / 2 - 2, dcy - ah / 2 + 2, al - aw / 2, ah - 4 }; SDL_RenderFillRect(ren, &r); }
}

/* ------------------------------------------------------------------ */
/* ROM File Picker, CRC32 & Main                                        */
/* ------------------------------------------------------------------ */

static uint32_t crc32(const uint8_t *d, size_t n)
{
    uint32_t c = 0xffffffffu;
    while (n--) { c ^= *d++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0u); }
    return ~c;
}

static void fatal(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Soccer Shootout", msg, NULL);
    exit(1);
}

static uint8_t *read_rom_file(const char *path, size_t *out_n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 0x400000) { fclose(f); return NULL; }
    uint8_t *b = (uint8_t *)malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f);
    if ((size_t)sz == ROM_SIZE + 512) { memmove(b, b + 512, ROM_SIZE); sz = (long)ROM_SIZE; }
    *out_n = (size_t)sz;
    return b;
}

static bool pick_rom_dialog(char *out, size_t cap)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    out[0] = 0;
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = "SNES ROM (*.sfc;*.smc)\0*.sfc;*.smc\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = out;
    ofn.nMaxFile = (DWORD)cap;
    ofn.lpstrTitle = T("Selecciona tu ROM de Capcom's Soccer Shootout (USA)",
                       "Select your Capcom's Soccer Shootout (USA) ROM");
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) != 0;
#elif !defined(__ANDROID__)
    const char *cmds[2] = {
        "zenity --file-selection --title='Select Capcom Soccer Shootout (USA) ROM' --file-filter='SNES ROMs | *.sfc *.smc' 2>/dev/null",
        "kdialog --getopenfilename . '*.sfc *.smc|SNES ROMs' 2>/dev/null"
    };
    for (int i = 0; i < 2; i++) {
        FILE *p = popen(cmds[i], "r");
        if (!p) continue;
        if (fgets(out, (int)cap, p)) {
            size_t len = strlen(out);
            while (len && (out[len - 1] == '\n' || out[len - 1] == '\r')) out[--len] = 0;
            pclose(p);
            if (len > 0) return true;
        } else pclose(p);
    }
    return false;
#else
    (void)out; (void)cap; return false;
#endif
}

static void save_shot(const char *path)
{
    static uint32_t out[W * H];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t u = ui[y * W + x], g = game[(y * gh / H) * gw + x * gw / W];
            unsigned a = u >> 24;
            uint32_t r  = (((u >> 16) & 255) * a + ((g >> 16) & 255) * (255 - a)) / 255;
            uint32_t gg = (((u >> 8) & 255) * a + ((g >> 8) & 255) * (255 - a)) / 255;
            uint32_t b  = ((u & 255) * a + (g & 255) * (255 - a)) / 255;
            out[y * W + x] = 0xff000000u | (r << 16) | (gg << 8) | b;
        }
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(out, W, H, 32, W * 4, SDL_PIXELFORMAT_ARGB8888);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}

static void init_dirs(void)
{
    char *bp = SDL_GetBasePath();
    snprintf(base_dir, sizeof base_dir, "%s", bp ? bp : "./");
    if (bp) SDL_free(bp);
#ifdef _WIN32
    snprintf(save_dir, sizeof save_dir, "%s", base_dir);
#elif defined(__ANDROID__)
    const char *ap = SDL_AndroidGetInternalStoragePath();
    snprintf(save_dir, sizeof save_dir, "%s/", ap ? ap : ".");
#else
    char *pp = SDL_GetPrefPath("Shootout26", "SoccerShootout");
    snprintf(save_dir, sizeof save_dir, "%s", pp ? pp : base_dir);
    if (pp) SDL_free(pp);
#endif
}

int main(int argc, char **argv)
{
    const char *rom_arg = NULL, *core_arg = NULL, *test_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--core") && i + 1 < argc) core_arg = argv[++i];
        else if (!strcmp(argv[i], "--test") && i + 1 < argc) test_dir = argv[++i];
        else rom_arg = argv[i];
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) fatal(SDL_GetError());
    init_dirs();
    cfg_load();
    guess_language();
    overclock = cfg.fastcpu;

    char path[1200];
    snprintf(path, sizeof path, "%s%s", base_dir, CORE_NAME);
    bool core_ok = load_core(core_arg ? core_arg : path);
#ifndef _WIN32
    if (!core_ok) core_ok = load_core(CORE_NAME);
#endif
    if (!core_ok)
        fatal(T("Falta " CORE_NAME " junto al programa.", CORE_NAME " is missing next to the program."));

    static const char *names[] = {
        "Capcom's Soccer Shootout (USA).sfc",
        "Capcom's Soccer Shootout (USA).smc",
        "soccer_shootout.sfc",
        "soccer.sfc",
        "rom.sfc"
    };
    size_t rom_n = 0;
    uint8_t *rom = NULL;
    char chosen_rom[1024] = "";

    if (rom_arg) { rom = read_rom_file(rom_arg, &rom_n); if (rom) snprintf(chosen_rom, sizeof chosen_rom, "%s", rom_arg); }
    if (!rom && cfg.rom_path[0]) { rom = read_rom_file(cfg.rom_path, &rom_n); if (rom) snprintf(chosen_rom, sizeof chosen_rom, "%s", cfg.rom_path); }
    for (int i = 0; !rom && i < 5; i++) {
        snprintf(path, sizeof path, "%s%s", base_dir, names[i]);
        rom = read_rom_file(path, &rom_n);
        if (rom) snprintf(chosen_rom, sizeof chosen_rom, "%s", path);
    }
    if (!rom) {
        snprintf(path, sizeof path, "%srom.sfc", save_dir);
        rom = read_rom_file(path, &rom_n);
        if (rom) snprintf(chosen_rom, sizeof chosen_rom, "%s", path);
    }
    if (!rom && !test_dir && pick_rom_dialog(chosen_rom, sizeof chosen_rom)) {
        rom = read_rom_file(chosen_rom, &rom_n);
    }
    if (!rom)
        fatal(T("No encuentro la ROM. Selecciona \"Capcom's Soccer Shootout (USA).sfc\" o ponla junto al programa.",
                "ROM not found. Select \"Capcom's Soccer Shootout (USA).sfc\" or place it next to the program."));

    if (rom_n != ROM_SIZE || crc32(rom, rom_n) != ROM_CRC) {
        free(rom);
        fatal(T("Esta ROM no es Capcom's Soccer Shootout (USA) (SNES 1994, 1.5MB) sin modificar.",
                "This ROM is not an unmodified Capcom's Soccer Shootout (USA) (SNES 1994, 1.5MB)."));
    }
    if (chosen_rom[0] && strcmp(cfg.rom_path, chosen_rom)) {
        snprintf(cfg.rom_path, sizeof cfg.rom_path, "%s", chosen_rom);
        cfg_save();
    }

    // Save clean original roster tables (T1 + T2), then apply user's chosen Roster Pack in memory!
    rom_work = rom;
    memcpy(orig_roster, rom_work + ROSTER_ROM_OFFSET, sizeof orig_roster);
    memcpy(orig_roster_t2, rom_work + ROSTER_T2_BASE, sizeof orig_roster_t2);
    apply_roster_pack(rom_work, orig_roster, orig_roster_t2, cfg.roster);

    struct retro_game_info gi = { "soccer.sfc", rom_work, ROM_SIZE, NULL };
    if (!core.load_game(&gi)) fatal(T("El nucleo no pudo cargar la ROM.", "The core could not load the ROM."));
    struct retro_system_av_info av;
    core.get_av(&av);

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = (int)(av.timing.sample_rate + 0.5);
    want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512;
    if (!test_dir) audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);

    if (!extract_art())
        fatal(T("No se pudo extraer el arte y la fuente de tu ROM.",
                "Could not extract menu art and font from your ROM."));

    if (test_dir) {
        char p[1200];
#define SHOT(name) (snprintf(p, sizeof p, "%s/" name ".bmp", test_dir), draw_menu(), save_shot(p))
        SHOT("01_start");
        view_team = 3; open_page(P_ROSTERS); SHOT("02_roster_arg_2026");
        page = P_START; open_page(P_OPTIONS); SHOT("03_options");

        // Launch Exhibition with Modern 2026 rosters -> USA vs Brazil lineup screen!
        script_launch_mode(0); // Exhibition -> Players
        for (int i = 0; i < 6; i++) tap(RETRO_DEVICE_ID_JOYPAD_A, 85); // 1PvCP, 5m, StadA, YES, USA, Brazil -> Lineup!
        run(60);
        memset(ui, 0, sizeof ui);
        snprintf(p, sizeof p, "%s/04_lineup_usa_bra_2026.bmp", test_dir); save_shot(p);
        printf("2026 lineup captured!\n");

        // Switch dynamically to 1994 Legends -> capture USA vs Brazil 1994 Legends lineup!
        reload_rom_with_roster(2);
        script_launch_mode(0);
        for (int i = 0; i < 6; i++) tap(RETRO_DEVICE_ID_JOYPAD_A, 85);
        run(60);
        memset(ui, 0, sizeof ui);
        snprintf(p, sizeof p, "%s/05_lineup_usa_bra_1994.bmp", test_dir); save_shot(p);
        printf("1994 Legends lineup captured!\n");

        // Launch Indoor Soccer 8v8 quick mode to confirm 6-mode scripting!
        script_launch_mode(4);
        run(90);
        memset(ui, 0, sizeof ui);
        snprintf(p, sizeof p, "%s/06_indoor_mode.bmp", test_dir); save_shot(p);

        pause_game(); SHOT("07_pause");
        open_page(P_MODS); SHOT("08_mods");
        return 0;
    }

    win = SDL_CreateWindow("Capcom's Soccer Shootout 2026", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) fatal(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Texture *tg = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 512, 512);
    SDL_Texture *tu = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    SDL_SetTextureBlendMode(tu, SDL_BLENDMODE_BLEND);
    set_fullscreen();
    if (audio) SDL_PauseAudioDevice(audio, 0);

    double frame_ms = 1000.0 / av.timing.fps;
    Uint64 next = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();
    Uint32 audio_target = (Uint32)(want.freq * 4 * 3 / 60);

    while (!quit) {
        int rw = 1024, rh = 768;
        SDL_GetRendererOutputSize(ren, &rw, &rh);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            else if (e.type == SDL_KEYDOWN && !e.key.repeat && binding >= 0) bind_key(e.key.keysym.scancode);
            else if (e.type == SDL_CONTROLLERBUTTONDOWN && binding >= 0) bind_pad(e.cbutton.button);
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    cfg.fullscreen ^= 1; set_fullscreen(); cfg_save();
                } else if (k == SDLK_F5 && state == ST_PLAY) save_state();
                else if (k == SDLK_F7 && state == ST_PLAY) load_state();
                else handle(key_action(k));
            } else if (e.type == SDL_KEYDOWN && e.key.repeat && state != ST_PLAY) {
                int m = key_action(e.key.keysym.sym);
                if (m == M_UP || m == M_DOWN) handle(m);
            } else if (e.type == SDL_CONTROLLERDEVICEADDED) {
                for (int i = 0; i < 4; i++) if (!pads[i]) { pads[i] = SDL_GameControllerOpen(e.cdevice.which); break; }
            } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                for (int i = 0; i < 4; i++)
                    if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == e.cdevice.which) {
                        SDL_GameControllerClose(pads[i]); pads[i] = NULL;
                    }
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                int b = e.cbutton.button;
                pad_active_recently = true; last_pad_tick = SDL_GetTicks();
                if (state == ST_PLAY && (b == SDL_CONTROLLER_BUTTON_LEFTSTICK || b == SDL_CONTROLLER_BUTTON_RIGHTSTICK)) load_state();
                else handle(pad_action(b));
            } else if (e.type == SDL_CONTROLLERAXISMOTION && state == ST_PLAY &&
                       (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
                static bool down[2];
                int t = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
                if (e.caxis.value > 20000 && !down[t]) { down[t] = true; pad_active_recently = true; last_pad_tick = SDL_GetTicks(); save_state(); }
                else if (e.caxis.value < 8000) down[t] = false;
            } else if (e.type == SDL_FINGERDOWN) {
                touch_seen = true; pad_active_recently = false;
                for (int i = 0; i < MAX_FINGERS; i++) {
                    if (!fingers[i].down) {
                        fingers[i].down = true; fingers[i].id = e.tfinger.fingerId;
                        fingers[i].x = e.tfinger.x; fingers[i].y = e.tfinger.y;
                        fingers[i].start_y = e.tfinger.y; fingers[i].scrolled = false;
                        break;
                    }
                }
                if (state == ST_PLAY) handle_pointer_tap(e.tfinger.x, e.tfinger.y, rw, rh);
            } else if (e.type == SDL_FINGERMOTION) {
                for (int i = 0; i < MAX_FINGERS; i++) {
                    if (fingers[i].down && fingers[i].id == e.tfinger.fingerId) {
                        fingers[i].x = e.tfinger.x; fingers[i].y = e.tfinger.y;
                        if (state != ST_PLAY && (e.tfinger.y - fingers[i].start_y > 0.055f)) {
                            handle(M_UP); fingers[i].start_y = e.tfinger.y; fingers[i].scrolled = true;
                        } else if (state != ST_PLAY && (fingers[i].start_y - e.tfinger.y > 0.055f)) {
                            handle(M_DOWN); fingers[i].start_y = e.tfinger.y; fingers[i].scrolled = true;
                        }
                        break;
                    }
                }
            } else if (e.type == SDL_FINGERUP) {
                for (int i = 0; i < MAX_FINGERS; i++) {
                    if (fingers[i].down && fingers[i].id == e.tfinger.fingerId) {
                        bool was_tap = !fingers[i].scrolled;
                        fingers[i].down = false;
                        if (state != ST_PLAY && was_tap) handle_pointer_tap(e.tfinger.x, e.tfinger.y, rw, rh);
                        break;
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.which != SDL_TOUCH_MOUSEID && e.button.button == 1) {
                if (rw > 0 && rh > 0) handle_pointer_tap((float)e.button.x / (float)rw, (float)e.button.y / (float)rh, rw, rh);
            }
        }

        poll_stick_menu();

        uint16_t h1 = 0, h2 = 0;
        read_both_pads(&h1, &h2);
        bool fast = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_TAB] != 0;
        if (state == ST_PLAY && (
                ((h1 & (1u << RETRO_DEVICE_ID_JOYPAD_SELECT)) && (h1 & (1u << RETRO_DEVICE_ID_JOYPAD_START))) ||
                ((h1 & (1u << RETRO_DEVICE_ID_JOYPAD_L)) && (h1 & (1u << RETRO_DEVICE_ID_JOYPAD_R)) && (h1 & (1u << RETRO_DEVICE_ID_JOYPAD_START)))
            ))
            pause_game();

        if (state == ST_PLAY) {
            pad_p1 = h1; pad_p2 = h2;
            int steps = fast ? 4 : (cfg.turbo && (++turbo_tick % 3 == 0) ? 2 : 1);
            mute = fast;
            for (int i = 0; i < steps; i++) { core.run(); match_hooks(); }
            mute = false;
            memset(ui, 0, sizeof ui);
            if (cfg.hud_banner && cfg.roster != 0) {
                const char *tag = (cfg.roster == 1) ? "2026" : "1994";
                for (int y = H - 12; y < H - 2; y++)
                    for (int x = W - 38; x < W - 2; x++) ui[y * W + x] = 0x88080c24u;
                text(W - 35, H - 11, tag, cfg.roster == 1 ? GREEN : GOLD);
            }
        } else {
            pad_p1 = 0; pad_p2 = 0;
            draw_menu();
        }
        draw_toast();

        SDL_UpdateTexture(tg, NULL, game, gw * 4);
        SDL_UpdateTexture(tu, NULL, ui, W * 4);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_Rect src = { 0, 0, gw, gh };
        SDL_Rect dst = compute_game_rect(rw, rh);
        SDL_RenderCopy(ren, tg, &src, &dst);
        SDL_RenderCopy(ren, tu, NULL, &dst);
        draw_touch_overlay(ren, rw, rh, h1);
        SDL_RenderPresent(ren);

        if (state == ST_PLAY && audio && !fast) {
            while (SDL_GetQueuedAudioSize(audio) > audio_target) SDL_Delay(1);
            next = SDL_GetPerformanceCounter();
        } else {
            if (audio && state != ST_PLAY) SDL_ClearQueuedAudio(audio);
            next += (Uint64)(frame_ms * freq / 1000.0);
            Uint64 now = SDL_GetPerformanceCounter();
            if (next > now) SDL_Delay((Uint32)((next - now) * 1000 / freq));
            else next = now;
        }
    }
    cfg_save();
    SDL_Quit();
    return 0;
}
