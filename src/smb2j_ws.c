/* smb2j_ws.c - Super Mario Bros. 2 (Japan) custom widescreen: the mod's
 * configuration, its compositor, hook sites and the game's additions to the
 * cycle host (cyc_host_extras.h). See docs/WIDESCREEN.md. */
#include "smb2j_ws.h"
#include "smb2j_ws_actors.h"
#include "smb2j_ws_world.h"

#include "cyc_host_extras.h"
#include "cyc_render.h"
#include "cyc_video.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include "mod_runtime.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint8_t *smb2j_ram;

static int s_enabled, s_edges = 1, s_room_edges = 1;
static int s_aspect = NES_VIDEO_FIT;
static SmbEnemyMode s_enemies = SMB_ENEMIES_VIEWPORT;
static uint64_t s_wide_frames, s_native_frames;
static uint8_t s_opaque[CYC_VIDEO_MAX_WIDTH * 240];
static int s_render_camera, s_render_native_x0, s_view_left;
static FILE *s_log;                 /* --widescreen-log */
static void log_frame(void);

/* ---- the machine ---- */

bool smb2j_call(uint16_t routine, uint8_t x) {
    static CycModRegs r;
    r.x = x;
    r.s = 0xfd;
    r.p = 0x24;
    return cyc_mod_call(routine, &r);
}

uint8_t smb2j_reg_x(void) {
    CycModRegs r;
    cyc_mod_regs(&r);
    return r.x;
}

bool smb2j_ws_game_engine(void) {
    return RAM(RAM_OperMode) == 1 && RAM(RAM_OperMode_Task) == SMB2J_GAME_ENGINE_TASK;
}

bool smb2j_ws_world_scene(void) {
    if (smb2j_ws_game_engine()) return true;
    /* Victory mode: the bridge, the walk and the castle; world 8's subs from
     * task 5 on are disk loads, the final room and the ending (SM2DATA3),
     * which are not area-parser scenes. World D runs them as world 8. */
    return RAM(RAM_OperMode) == 2 && !(RAM(RAM_WorldNumber) == SMB2J_WORLD8 && RAM(RAM_OperMode_Task) >= 5);
}

static int camera_x(void) { return RAM(RAM_ScreenLeft_PageLoc) << 8 | RAM(RAM_ScreenLeft_X_Pos); }

static int anchor_camera(int width) {
    return s_enabled && s_room_edges && width > 256 && s_enemies != SMB_ENEMIES_NATIVE && smb2j_ws_world_scene() &&
           g_smb_ws_world.valid;
}

int smb2j_ws_view_left(int native_camera, int width) {
    int view = native_camera - (width - 256) / 2;
    if (!anchor_camera(width)) return view;
    int left, right;
    smb_ws_world_bounds(native_camera, &left, &right);
    if (right - left <= width) return left - (width - (right - left)) / 2;
    if (view < left) view = left;
    if (view > right - width) view = right - width;
    return view;
}

void smb2j_ws_actor_range(int native_camera, int width, int pad, int *left, int *right) {
    *left = smb2j_ws_view_left(native_camera, width) - pad;
    *right = *left + width + pad * 2;
    if (anchor_camera(width)) {
        int start, end;
        smb_ws_world_bounds(native_camera, &start, &end);
        if (*left < start - pad) *left = start - pad;
        if (*right > end + pad) *right = end + pad;
    }
}

/* The playfield line whose captured scroll is the playfield's: below the
 * status bar, whose FDS IRQ split rewrites PPUCTRL and the scroll
 * (IRQHandler), well above any sprite-0-free bottom line. */
enum { PLAYFIELD_LINE = 40 };

static int render_camera(void) {
    /* Game logic has already advanced the camera for the next frame. Match
     * the scroll the PPU actually rendered this picture with (captured per
     * line), then unwrap its two nametables against the absolute camera. */
    int ppu = cyc_render_line_scroll_x(PLAYFIELD_LINE), cam = camera_x();
    return cam + ((ppu - cam + 256) & 511) - 256;
}

/* ---- the compositor ---- */

static int render(uint32_t *out, int width, int height, int native_x0, const uint32_t *native, void *user) {
    (void)user;
    if (!s_enabled || !smb2j_ws_world_scene() || !g_smb_ws_world.valid ||
        !(cyc_render_line_mask(PLAYFIELD_LINE) & 0x08)) {
        s_native_frames++;
        return 0;
    }
    int cam = render_camera();
    s_render_camera = cam;
    s_view_left = smb2j_ws_view_left(cam, width);
    int play_x0 = cam - s_view_left;
    s_render_native_x0 = play_x0;
    int room_left = 0, room_right = SMB_WS_META_COLUMNS * 16;
    if (anchor_camera(width)) smb_ws_world_bounds(cam, &room_left, &room_right);
    uint32_t backdrop = cyc_render_color(0), colors[4][4];
    for (int p = 0; p < 4; ++p) for (int c = 0; c < 4; ++c) colors[p][c] = c ? cyc_render_color(p * 4 + c) : backdrop;
    uint16_t pattern = cyc_render_line_bg_table(PLAYFIELD_LINE);
    for (int y = 0; y < height; y++) {
        uint8_t *op = &s_opaque[y * width];
        uint32_t *row = &out[y * width];
        for (int x = 0; x < width;) {
            int wx = s_view_left + x;
            uint8_t tile, palette;
            if (wx >= room_left && wx < room_right && smb_ws_world_pixel(wx, y, &palette, &tile)) {
                /* one pattern row covers the rest of this 8-pixel tile */
                uint8_t lo = cyc_render_chr((uint16_t)(pattern + tile * 16 + (y & 7)));
                uint8_t hi = cyc_render_chr((uint16_t)(pattern + tile * 16 + (y & 7) + 8));
                int end = x + (8 - (wx & 7));
                if (end > width) end = width;
                for (; x < end; ++x, ++wx) {
                    int shift = 7 - (wx & 7), pixel = ((lo >> shift) & 1) | (((hi >> shift) & 1) << 1);
                    row[x] = colors[palette & 3][pixel];
                    op[x] = pixel != 0;
                }
            } else {
                row[x] = backdrop;
                op[x] = 0;
                ++x;
            }
        }
    }
    /* The native pass remains authoritative for native sprites, priority,
     * transient tiles and the original play area. */
    const uint8_t *bg = cyc_frame_bg_opaque();
    int first = play_x0 < 0 ? -play_x0 : 0;
    int last = play_x0 + 256 > width ? width - play_x0 : 256;
    for (int y = SMB2J_HUD_ROWS; y < height; y++) for (int x = first; x < last; x++) {
        out[y * width + x + play_x0] = native[y * 256 + x];
        s_opaque[y * width + x + play_x0] = bg[y * 256 + x];
    }
    smb_ws_actors_draw(out, width, height, play_x0, cam, s_opaque);
    for (int y = 0; y < SMB2J_HUD_ROWS; y++) {
        if (s_edges) {
            memcpy(out + y * width, native + y * 256, 128 * sizeof(uint32_t));
            memcpy(out + y * width + width - 128, native + y * 256 + 128, 128 * sizeof(uint32_t));
        } else {
            memcpy(out + y * width + native_x0, native + y * 256, 256 * sizeof(uint32_t));
        }
    }
    s_wide_frames++;
    return 1;
}

/* ---- configuration ---- */

static bool s_ready;
static void apply(void) {
    if (!s_ready) return;
    if (s_enabled) {
        cyc_render_set_compositor(render, NULL);
        cyc_video_set_mode(s_aspect);
    } else {
        cyc_render_set_compositor(NULL, NULL);
        cyc_video_set_mode(NES_VIDEO_STOCK);
    }
    nes_mod_set_function_hook_enabled("smb2j.widescreen.column", s_enabled);
    nes_mod_set_function_hook_enabled("smb2j.widescreen.area", s_enabled);
    nes_mod_set_function_hook_enabled("smb2j.widescreen.collision-column", s_enabled);
    smb_ws_actors_configure(s_enabled, s_enemies);
}

void smb2j_ws_set_mod_enabled(int enabled) {
    s_enabled = enabled != 0;
    if (!s_enabled) {
        s_aspect = NES_VIDEO_FIT;
        s_edges = s_room_edges = 1;
        s_enemies = SMB_ENEMIES_VIEWPORT;
    }
    apply();
}

void smb2j_ws_configure(const char *aspect, const char *hud, const char *enemies) {
    int value;
    if (nes_video_geometry_parse(aspect, &value) && value != NES_VIDEO_STOCK) s_aspect = value;
    s_edges = !hud || strcmp(hud, "center") != 0;
    s_enemies = enemies && !strcmp(enemies, "classic") ? SMB_ENEMIES_CLASSIC : SMB_ENEMIES_VIEWPORT;
    apply();
}

void smb2j_ws_set_camera(const char *camera) {
    s_room_edges = !camera || strcmp(camera, "centered") != 0;
}

/* ---- hook sites (game.toml) ---- */

static int column_hook(uint16_t addr) {
    (void)addr;
    if (s_enabled && !smb_ws_world_busy() && RAM(RAM_OperMode) != 0) smb_ws_world_observe_column();
    return 0; /* observe only; the original routine always executes */
}

static int area_hook(uint16_t addr) {
    (void)addr;
    if (s_enabled && !smb_ws_world_busy()) { smb_ws_world_reset(); smb_ws_actors_reset(); }
    return 0;
}

static int collision_column_hook(uint16_t addr) {
    (void)addr;
    if (s_enabled) smb_ws_world_begin_column();
    return 0;
}

/* ---- the developer start fixture ----
 * --dev-start W:L:A (world, level, area numbers, 0-based; W 9-12 = worlds
 * A-D) starts a game there: right after the title's StartGame (attract task
 * 5, HardWorldsCheckpoint, before it runs) it sets WorldNumber/LevelNumber/
 * AreaNumber and HardWorldFlag, and the game's own disk routines load that
 * world's files (SM2DATA4 for A-D there, SM2DATA2 for 5-8 in GameModeDisk-
 * Routines). World 9 lives in SM2DATA3, which only the ending loads: not a
 * start. A test fixture, not a player route. */
static int s_dev_world = -1, s_dev_level, s_dev_area, s_dev_done;

static void dev_start(void) {
    if (s_dev_world < 0 || s_dev_done || RAM(RAM_OperMode) != 0 || RAM(RAM_OperMode_Task) != 5) return;
    int hard = s_dev_world >= 9;
    POKE(RAM_WorldNumber, (uint8_t)(hard ? s_dev_world - 9 : s_dev_world));
    POKE(RAM_LevelNumber, (uint8_t)s_dev_level);
    POKE(RAM_AreaNumber, (uint8_t)s_dev_area);
    POKE(RAM_HardWorldFlag, (uint8_t)hard);
    s_dev_done = 1;
    printf("[Widescreen] dev start: world %d level %d area %d%s\n", hard ? s_dev_world - 9 : s_dev_world, s_dev_level,
           s_dev_area, hard ? " (letter worlds)" : "");
    fflush(stdout);
}

/* ---- the game's additions to the host ---- */

static void x_power_on(void *ctx) {
    (void)ctx;
    smb2j_ram = cyc_cpu_ram();
    smb_ws_world_reset();
    smb_ws_actors_reset();
    s_wide_frames = s_native_frames = 0;
    s_dev_done = 0;
    s_ready = true;
    apply();
}

static void x_frame_begin(void *ctx) {
    (void)ctx;
    if (s_enabled) smb_ws_actors_begin_frame();
}

static void x_frame_end(void *ctx) {
    (void)ctx;
    dev_start();
    if (s_enabled && smb2j_ws_world_scene()) {
        smb_ws_world_update();
        smb_ws_actors_update();
    }
    if (s_log) log_frame();
}

static const CycHostOption OPTIONS[] = {
    { "--widescreen", true, "fit | 16:9 | 21:9 | 32:9 | off: enable the widescreen mod at that aspect (overrides Mods)" },
    { "--widescreen-camera", true, "edges | centered" },
    { "--widescreen-hud", true, "edges | center" },
    { "--widescreen-enemies", true, "viewport | classic (native: renderer diagnostics only)" },
    { "--dev-start", true, "W:L:A world, level and area numbers to start a game at (test fixture; W 9-12 = A-D)" },
    { "--widescreen-log", true, "FILE: one JSON line per frame (the mod's state, seams, residents; probes)" },
};

static bool x_option(void *ctx, const char *name, const char *value) {
    (void)ctx;
    if (!strcmp(name, "--widescreen-camera")) {
        if (strcmp(value, "edges") && strcmp(value, "centered")) return false;
        smb2j_ws_set_camera(value);
        return true;
    }
    if (!strcmp(name, "--widescreen-hud")) {
        if (strcmp(value, "edges") && strcmp(value, "center")) return false;
        s_edges = !strcmp(value, "edges");
        return true;
    }
    if (!strcmp(name, "--widescreen-enemies")) {
        if (!strcmp(value, "classic")) s_enemies = SMB_ENEMIES_CLASSIC;
        else if (!strcmp(value, "viewport")) s_enemies = SMB_ENEMIES_VIEWPORT;
        else if (!strcmp(value, "native")) s_enemies = SMB_ENEMIES_NATIVE;
        else return false;
        apply();
        return true;
    }
    if (!strcmp(name, "--widescreen")) {
        int aspect;
        if (!strcmp(value, "off")) { smb2j_ws_set_mod_enabled(0); return true; }
        if (!nes_video_geometry_parse(value, &aspect) || aspect == NES_VIDEO_STOCK) return false;
        s_aspect = aspect;
        s_enabled = 1;
        apply();
        return true;
    }
    if (!strcmp(name, "--widescreen-log")) {
        s_log = fopen(value, "w");
        return s_log != NULL;
    }
    if (!strcmp(name, "--dev-start")) {
        int w, l, a;
        if (sscanf(value, "%d:%d:%d", &w, &l, &a) != 3 || w < 0 || w > 12 || w == 8 || l < 0 || l > 3 || a < 0 || a > 7)
            return false;
        s_dev_world = w;
        s_dev_level = l;
        s_dev_area = a;
        return true;
    }
    return false;
}

/* The terrain against the native picture's background near both joins, at
 * offsets -4..+4 from the camera the compositor uses: a scroll-phase error
 * shows as the minimum away from offset 0. Measured on the frame just drawn
 * (the native background's coverage, cyc_frame_bg_opaque), with or without a
 * composed picture. */
static int seam_json(char *buf, int cap) {
    int errors[9] = { 0 }, samples = 0;
    const uint8_t *bg = cyc_frame_bg_opaque();
    int cam = s_enabled && g_smb_ws_world.valid && smb2j_ws_world_scene() ? render_camera() : -1;
    if (cam >= 0) {
        uint16_t pattern = cyc_render_line_bg_table(PLAYFIELD_LINE);
        for (int y = SMB2J_HUD_ROWS + 8; y < 232; y++) for (int side = 0; side < 2; side++)
            for (int x = 8 + side * 224; x < 24 + side * 224; x++) {
                int actual = bg[y * 256 + x];
                samples++;
                for (int d = -4; d <= 4; d++) {
                    uint8_t pal, tile;
                    int wx = cam + x + d, pixel = 0;
                    if (smb_ws_world_pixel(wx, y, &pal, &tile)) {
                        int off = pattern + tile * 16 + (y & 7), bit = 7 - (wx & 7);
                        pixel = ((cyc_render_chr((uint16_t)off) >> bit) & 1) |
                                (((cyc_render_chr((uint16_t)(off + 8)) >> bit) & 1) << 1);
                    }
                    errors[d + 4] += (pixel != 0) != actual;
                }
            }
    }
    return snprintf(buf, (size_t)cap,
                    "\"render_camera\":%d,\"game_camera\":%d,\"ppu_scroll_x\":%d,\"samples\":%d,"
                    "\"offset_errors\":[%d,%d,%d,%d,%d,%d,%d,%d,%d]",
                    cam, camera_x(), cyc_render_line_scroll_x(PLAYFIELD_LINE), samples, errors[0], errors[1],
                    errors[2], errors[3], errors[4], errors[5], errors[6], errors[7], errors[8]);
}

static int state_json(char *buf, int cap) {
    const SmbWsWorld *w = &g_smb_ws_world;
    return snprintf(buf, (size_t)cap,
             "\"enabled\":%d,\"compositor\":%d,\"render_width\":%d,\"camera_x\":%d,\"area_data\":%u,\"file_list\":%u,"
             "\"hard_world\":%u,\"world\":%u,\"view_left\":%d,\"native_x0\":%d,\"room_edges\":%d,\"hud_edges\":%d,"
             "\"enemies\":%d,\"area_end\":%u,\"fixed_rooms\":%u,\"valid\":%u,\"decoded_columns\":%u,"
             "\"verified_columns\":%u,\"mismatched_columns\":%u,\"first_mismatch_column\":%d,\"first_mismatch_row\":%d,"
             "\"expected\":%u,\"actual\":%u,\"decode_cycles\":%u,\"plants\":%u,\"flag_x\":%d,\"world_scene\":%d,"
             "\"game_engine\":%d,\"wide_frames\":%llu,\"native_frames\":%llu,\"oper_mode\":%u,\"oper_task\":%u,"
             "\"world_number\":%u,\"level\":%u,\"area\":%u,\"area_type\":%u",
             s_enabled, cyc_render_has_compositor(), cyc_video_width(), camera_x(), w->area_data, w->file_list,
             w->hard_world, w->world, smb2j_ws_view_left(camera_x(), cyc_video_width()), s_render_native_x0,
             s_room_edges, s_edges, s_enemies, w->area_end, w->fixed_rooms, w->valid, w->decoded_columns,
             w->verified_columns, w->mismatched_columns, w->first_mismatch_column, w->first_mismatch_row,
             w->first_expected, w->first_actual, w->decode_cycles, w->plant_count, w->flag_x, smb2j_ws_world_scene(),
             smb2j_ws_game_engine(), (unsigned long long)s_wide_frames, (unsigned long long)s_native_frames,
             RAM(RAM_OperMode), RAM(RAM_OperMode_Task), RAM(RAM_WorldNumber), RAM(RAM_LevelNumber),
             RAM(RAM_AreaNumber), RAM(RAM_AreaType));
}

/* --widescreen-log FILE: one JSON line per frame from power-on - the mod's
 * state, the seam measurement, the residents - for the probes
 * (tests/widescreen_probe.py). */
static long  s_log_frame;

static void log_frame(void) {
    static char buf[40000];
    int n = snprintf(buf, sizeof buf, "{\"frame\":%ld,", s_log_frame++);
    n += state_json(buf + n, (int)sizeof buf - n);
    buf[n++] = ',';
    n += seam_json(buf + n, (int)sizeof buf - n);
    buf[n++] = ',';
    n += smb_ws_actors_json(buf + n, (int)sizeof buf - n);
    fprintf(s_log, "%.*s}\n", n, buf);
}

#ifdef CYC_WITH_SDL
static void tcp_seam(int id, const char *line) {
    (void)line;
    char f[600];
    seam_json(f, (int)sizeof f);
    cyc_tcp_ok(id, f);
}

static void tcp_state(int id, const char *line) {
    (void)line;
    char f[1600];
    state_json(f, (int)sizeof f);
    cyc_tcp_ok(id, f);
}

/* Test fixture: write CPU RAM (RAM or PRG RAM addresses), e.g. to hold a
 * timer. Never a player feature. */
static void tcp_poke(int id, const char *line) {
    long addr = -1, value = -1;
    if (!cyc_tcp_long(line, "addr", &addr) || !cyc_tcp_long(line, "value", &value) || value < 0 || value > 255 ||
        !cyc_mod_poke((uint16_t)addr, (uint8_t)value)) {
        cyc_tcp_err(id, "smb_ws_poke: addr (RAM or PRG RAM), value 0-255");
        return;
    }
    cyc_tcp_ok(id, NULL);
}

static void x_tcp_setup(void *ctx) {
    (void)ctx;
    cyc_tcp_register("smb_ws_state", "widescreen: mode, camera, world cache, frames", tcp_state);
    cyc_tcp_register("smb_ws_seam", "widescreen: terrain vs native background at both joins", tcp_seam);
    cyc_tcp_register("smb_ws_poke", "test fixture: write RAM / PRG RAM (addr, value)", tcp_poke);
    smb_ws_actors_tcp_setup();
}
#endif

static const CycHostExtras EXTRAS = {
    .ctx = NULL,
    .power_on = x_power_on,
    .frame_begin = x_frame_begin,
    .frame_end = x_frame_end,
    .options = OPTIONS,
    .option_count = sizeof OPTIONS / sizeof OPTIONS[0],
    .option = x_option,
#ifdef CYC_WITH_SDL
    .tcp_setup = x_tcp_setup,
#endif
};

const CycHostExtras *cyc_host_extras(void) { return &EXTRAS; }

/* ---- save states ---- */
static int save_world(uint8_t *data, int cap) {
    if (cap < (int)sizeof g_smb_ws_world) return -1;
    memcpy(data, &g_smb_ws_world, sizeof g_smb_ws_world);
    return sizeof g_smb_ws_world;
}
static int validate_world(const uint8_t *data, int len) {
    uint32_t version;
    if (!len) return 1;
    if (len != (int)sizeof g_smb_ws_world) return 0;
    memcpy(&version, data, sizeof version);
    return version == SMB_WS_WORLD_VERSION;
}
static int load_world(const uint8_t *data, int len) {
    if (!len) { smb_ws_world_reset(); return 1; }
    if (!validate_world(data, len)) return 0;
    memcpy(&g_smb_ws_world, data, sizeof g_smb_ws_world);
    /* Settings belong to Mods, never to the save being loaded. */
    return 1;
}

NES_MOD_CONSTRUCTOR(register_smb2j_widescreen) {
    if (!nes_mod_register_function_entry_plugin("smb2j.widescreen.column", SMB2J_RenderAreaGraphics, column_hook) ||
        !nes_mod_register_function_entry_plugin("smb2j.widescreen.area", SMB2J_InitializeArea, area_hook) ||
        !nes_mod_register_function_entry_plugin("smb2j.widescreen.collision-column", SMB2J_AreaParserCore,
                                                collision_column_hook) ||
        !nes_mod_register_savestate_hook("smb2j.widescreen.world", save_world, load_world) ||
        !nes_mod_register_savestate_validator("smb2j.widescreen.world", validate_world))
        fprintf(stderr, "[Widescreen] Failed to register the world cache hooks\n");
}
