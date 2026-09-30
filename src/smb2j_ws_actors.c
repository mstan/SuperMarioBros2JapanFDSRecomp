/* Authored enemy residents. The original five interaction slots still run in
 * the game. Residents outside those slots use its original init, graphics,
 * terrain collision and movement routines on an isolated machine
 * (cyc_mod.h), without guest time. World coordinates never depend on the
 * widened screen edge.
 *
 * SMB2J: enemy streams live in PRG RAM; the upside-down piranha plant (id
 * $04, worlds 5-8 and A-D) is a plant like $0D - created by the area parser,
 * initialized by the shared InitPiranhaPlant, moved by the overlay's
 * MoveUpsideDownPiranhaP through EnemyMovementSubs - and InitPiranhaPlant /
 * EnemyGfxHandler patch world-dependent attribute bytes in PRG RAM, which an
 * isolated call undoes and a committed one keeps (docs/SMB2J_SYMBOL_MAP.md). */
#include "smb2j_ws_actors.h"
#include "smb2j_ws_world.h"

#include "cyc_render.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include "mod_runtime.h"
#ifdef CYC_WITH_SDL
#include "cyc_tcp.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_ACTORS = 384, SPRITES = 6, PAD = 48 };
/* Every independently indexed normal-enemy field, including aliases used by
 * plants, flying koopas, squids, hammers and score popups. Slot zero is the
 * workspace; a transfer copies fields, never the surrounding shared RAM. */
static const uint16_t fields[] = {
    0x0f, 0x16, 0x1e, 0x46, 0x58, 0x6e, 0x87, 0xa0, 0xb6, 0xcf,
    0x3c5, 0x401, 0x417, 0x434, 0x491, 0x49a, 0x3d8, 0x78a, 0x796,
    0x3c, 0x3a2, 0x110, 0x12c, 0x117, 0x11e, 0x125,
};
enum { F_FLAG, F_ID, F_STATE, F_DIR, F_XS, F_PAGE, F_X, F_YS, F_YH, F_Y,
       F_ATTR, F_XF, F_YD, F_YF, F_COLL, F_BBOX, F_OFF, F_TIMER, F_INTERVAL,
       FIELD_COUNT = sizeof fields / sizeof fields[0] };
typedef struct { int16_t x, y; uint8_t tile, attr; } Sprite;
typedef struct { uint8_t count, native, oam_base; Sprite sprite[SPRITES]; } Packet;
typedef struct {
    uint16_t spawn_x;
    uint8_t offset, member, spawn_y, kind, loaded, active, dead;
    int16_t native_slot;
    uint8_t state[FIELD_COUNT];
} Actor;
typedef struct {
    uint32_t version;
    uint16_t area, enemy_data, count;
    uint8_t world, file_list, hard_world;
    int16_t owner[5];
    Actor actor[MAX_ACTORS];
    Packet display[MAX_ACTORS], next[MAX_ACTORS];
    Packet flag_display, flag_next;
    uint8_t contacts[MAX_ACTORS][MAX_ACTORS / 8];
    int camera;
    uint64_t updates, loads, transfers;
} Actors;
#define ACTORS_VERSION 5u
static Actors s;
static int s_enabled, s_virtual, s_render_wide;
static SmbEnemyMode s_mode = SMB_ENEMIES_VIEWPORT;

static int camera(void) { return RAM(RAM_ScreenLeft_PageLoc) << 8 | RAM(RAM_ScreenLeft_X_Pos); }
static int actor_x(const Actor *a) { return a->state[F_PAGE] << 8 | a->state[F_X]; }
static int live_x(int slot) { return RAM((uint16_t)(RAM_Enemy_PageLoc + slot)) << 8 | RAM((uint16_t)(RAM_Enemy_X_Position + slot)); }
static int gameplay(void) { return smb2j_ws_game_engine(); }
static int plant(int id) { return id == 0x0d || id == 0x04; }
/* Balance platforms ($24) reference another native slot and need paired
 * ownership. Independent platforms retain all movement state in one actor. */
static int platform(int id) { return id >= 0x25 && id <= 0x2c; }
static int managed(int id) { return (id <= 0x10 && id != 4 && id != 9) || platform(id); }

static void pull(Actor *a, int slot) {
    for (int i = 0; i < FIELD_COUNT; i++) a->state[i] = RAM((uint16_t)(fields[i] + slot));
}
static void push(const Actor *a, int slot) {
    for (int i = 0; i < FIELD_COUNT; i++) POKE((uint16_t)(fields[i] + slot), a->state[i]);
}

void smb_ws_actors_reset(void) {
    memset(&s, 0, sizeof s);
    s.version = ACTORS_VERSION;
    for (int i = 0; i < 5; i++) s.owner[i] = -1;
}

/* An isolated scope with the actor in slot 0 and its surroundings. */
static int begin(const Actor *a) {
    if (!cyc_mod_isolate_begin()) return 0;
    s_virtual = 1;
    for (int i = 0; i < 6; ++i) POKE((uint16_t)(RAM_Enemy_Flag + i), 0);
    push(a, 0);
    POKE(RAM_ObjectOffset, 0);
    /* Local screen projection serves only private graphics/bounding-box work.
     * Player/world coordinates and authored spawn positions remain absolute. */
    int cam = actor_x(a) - 128;
    POKE(RAM_ScreenLeft_PageLoc, (uint8_t)(cam >> 8));
    POKE(RAM_ScreenLeft_X_Pos, (uint8_t)cam);
    POKE(RAM_ScreenRight_PageLoc, (uint8_t)((cam + 255) >> 8));
    POKE(RAM_ScreenRight_X_Pos, (uint8_t)(cam + 255));
    for (unsigned k = 0; k < 0x1a0; ++k) POKE((uint16_t)(RAM_Block_Buffer_1 + k), 0);
    int first = actor_x(a) / 16 - 16;
    for (int c = first; c < first + 32; c++) {
        if (c < 0 || c >= SMB_WS_META_COLUMNS) continue;
        int address = RAM_Block_Buffer_1 + ((c & 16) ? 0xd0 : 0) + (c & 15);
        for (int y = 0; y < 13; y++) POKE((uint16_t)(address + y * 16), g_smb_ws_world.collision[c][y]);
    }
    POKE(RAM_Enemy_SprDataOffset, 0x40);
    POKE(RAM_BowserGfxFlag, 0);
    for (int i = 0; i < 64; i++) POKE((uint16_t)(0x200 + i * 4), 0xf8);
    return 1;
}
static void end(void) {
    cyc_mod_isolate_end();
    s_virtual = 0;
}

static void capture_packet(Packet *p, int native, int oam_base) {
    int wx = live_x(0);
    p->count = 0;
    p->native = (uint8_t)native;
    p->oam_base = (uint8_t)oam_base;
    for (int i = 0; i < SPRITES; i++) {
        uint16_t o = (uint16_t)(0x240 + i * 4);
        if (RAM(o) >= 0xef) continue;
        Sprite *v = &p->sprite[p->count++];
        v->x = (int16_t)(wx + (int)RAM((uint16_t)(o + 3)) - 128);
        v->y = (int16_t)(RAM(o) + 1);
        v->tile = RAM((uint16_t)(o + 1));
        v->attr = RAM((uint16_t)(o + 2));
    }
}

static void graphics(Packet *p, int native, int oam_base) {
    smb2j_call(SMB2J_GetEnemyOffscreenBits, 0);
    smb2j_call(SMB2J_RelativeEnemyPosition, 0);
    if (platform(RAM(RAM_Enemy_ID))) {
        if (RAM(RAM_Enemy_ID) >= 0x2b) smb2j_call(SMB2J_DrawSmallPlatform, 0);
        else smb2j_call(SMB2J_DrawLargePlatform, 0);
    } else {
        smb2j_call(SMB2J_EnemyGfxHandler, 0);
    }
    capture_packet(p, native, oam_base);
}

static void flag_graphics(Packet *p, const Actor *a, int native, int oam_base) {
    if (!begin(a)) return;
    if (!native) { POKE(RAM_FlagpoleCollisionYPos, 0); POKE(RAM_FlagpoleFNum_Y_Pos, 0xb0); }
    smb2j_call(SMB2J_GetEnemyOffscreenBits, 0);
    smb2j_call(SMB2J_RelativeEnemyPosition, 0);
    smb2j_call(SMB2J_FlagpoleGfxHandler, 0);
    capture_packet(p, native, oam_base);
    end();
}

static void add(int ofs, int member, int wx, int y, int kind) {
    if (s.count >= MAX_ACTORS) return;
    Actor *a = &s.actor[s.count++];
    a->offset = (uint8_t)ofs;
    a->member = (uint8_t)member;
    a->spawn_x = (uint16_t)wx;
    a->spawn_y = (uint8_t)y;
    a->kind = (uint8_t)kind;
    a->native_slot = -1;
}

static int ensure(void) {
    if (!s_enabled || s_virtual || !g_smb_ws_world.valid) return 0;
    unsigned enemy = RAM(RAM_EnemyData) | RAM(RAM_EnemyData + 1) << 8;
    if (enemy < 0x6000 || enemy > 0xDFFF) return 0;
    if (s.area == g_smb_ws_world.area_data && s.enemy_data == enemy && s.world == RAM(RAM_WorldNumber) &&
        s.file_list == RAM(RAM_FileListNumber) && s.hard_world == RAM(RAM_HardWorldFlag))
        return 1;
    smb_ws_actors_reset();
    s.area = g_smb_ws_world.area_data;
    s.enemy_data = (uint16_t)enemy;
    s.world = RAM(RAM_WorldNumber);
    s.file_list = RAM(RAM_FileListNumber);
    s.hard_world = RAM(RAM_HardWorldFlag);
    int page = 0, selected = 0;
    for (int ofs = 0; ofs < 255;) {
        int b0 = PRG((uint16_t)(enemy + ofs)), b1 = PRG((uint16_t)(enemy + ofs + 1)), row = b0 & 15;
        if (b0 == 0xff) break;
        if ((b1 & 128) && !selected) { page++; selected = 1; }
        if (row == 15 && !selected) { page = b1 & 63; selected = 1; ofs += 2; continue; }
        if (row < 14 && (!(b1 & 64) || RAM(RAM_SecondaryHardMode))) {
            int id = b1 & 63, wx = page * 256 + (b0 & 0xf0);
            if (managed(id)) add(ofs, 0, wx, row * 16, (id == 6 && RAM(RAM_PrimaryHardMode)) ? 2 : id);
            else if (id >= 0x37 && id <= 0x3e) {
                int group = id - 0x37, kind = group < 4 ? (RAM(RAM_PrimaryHardMode) ? 2 : 6) : 0;
                /* A group record marks its activation boundary, not its first
                 * body. CheckRightBounds admits it at native right+48, and
                 * HandleGroupEnemies places bodies from that native edge. */
                for (int n = 0; n < 2 + (group & 1); n++) add(ofs, n, wx - 48 + n * 24, (group & 2) ? 0x70 : 0xb0, kind);
            }
        }
        ofs += row == 14 ? 3 : 2;
        selected = 0;
    }
    for (int i = 0; i < g_smb_ws_world.plant_count; i++) {
        const SmbWsPlant *p = &g_smb_ws_world.plants[i];
        add(255, 0, p->x, p->y, p->kind);
    }
    return 1;
}

static void initialize(Actor *a) {
    memset(a->state, 0, sizeof a->state);
    a->state[F_FLAG] = 1;
    a->state[F_ID] = a->kind;
    a->state[F_PAGE] = (uint8_t)(a->spawn_x >> 8);
    a->state[F_X] = (uint8_t)a->spawn_x;
    a->state[F_YH] = 1;
    a->state[F_Y] = a->spawn_y;
    if (!begin(a)) return;
    if (a->offset == 255 && plant(a->kind)) smb2j_call(SMB2J_InitPiranhaPlant, 0);
    else smb2j_call(SMB2J_CheckpointEnemyID, 0);
    pull(a, 0);
    end();
    a->loaded = 1;
    s.loads++;
}

static void detach(int slot) {
    int n = s.owner[slot];
    if (n >= 0) { pull(&s.actor[n], slot); s.actor[n].native_slot = -1; }
    s.owner[slot] = -1;
    POKE((uint16_t)(RAM_Enemy_Flag + slot), 0);
}

static void clear_collision_bits(int slot) {
    for (int j = 0; j < 5; j++)
        POKE((uint16_t)(RAM_Enemy_CollisionBits + j),
             (uint8_t)(RAM((uint16_t)(RAM_Enemy_CollisionBits + j)) & ~(0x80u >> slot)));
}

static void promote(int n) {
    Actor *a = &s.actor[n];
    int slot = -1;
    for (int j = 0; j < 5; j++) if (!RAM((uint16_t)(RAM_Enemy_Flag + j)) && s.owner[j] != -2) { slot = j; break; }
    /* Keep the scarce original interaction slots closest to Mario. */
    if (slot < 0) {
        int px = RAM(RAM_Player_PageLoc) << 8 | RAM(RAM_Player_X_Position), far = abs(actor_x(a) - px) + 32;
        for (int j = 0; j < 5; j++) if (s.owner[j] >= 0) {
            int distance = abs(live_x(j) - px);
            if (distance > far && distance > 80) { slot = j; far = distance; }
        }
        if (slot >= 0) detach(slot);
    }
    if (slot < 0) return;
    a->state[F_COLL] = 0;
    clear_collision_bits(slot);
    push(a, slot);
    s.owner[slot] = (int16_t)n;
    a->native_slot = (int16_t)slot;
    s.transfers++;
}

static void collide_residents(void) {
    if (!(RAM(RAM_FrameCounter) & 1) || RAM(RAM_TimerControl) || (RAM(RAM_GamePauseStatus) & 1) || !RAM(RAM_AreaType)) return;
    for (int n = 0; n < s.count; n++) for (int m = n + 1; m < s.count; m++) {
        Actor *a = &s.actor[n], *b = &s.actor[m];
        uint8_t *contact = &s.contacts[n][m / 8], mask = (uint8_t)(1u << (m & 7));
        int ay = a->state[F_Y] + a->state[F_YH] * 256, by = b->state[F_Y] + b->state[F_YH] * 256;
        if (platform(a->kind) || platform(b->kind) || !a->loaded || !b->loaded || !a->active || !b->active ||
            a->dead || b->dead || abs(actor_x(a) - actor_x(b)) > 48 || abs(ay - by) > 48) {
            *contact &= (uint8_t)~mask;
            continue;
        }
        if (a->native_slot >= 0 && b->native_slot >= 0) continue; /* native game handled this pair */
        if (!begin(a)) continue;
        push(b, 1);
        /* A pair has a persistent latch of its own. Native collision bits are
         * indexed by the five transient slots and cannot identify residents. */
        POKE(RAM_Enemy_CollisionBits, (*contact & mask) ? 0x40 : 0);
        for (int slot = 0; slot < 2; slot++) {
            POKE(RAM_ObjectOffset, (uint8_t)slot);
            smb2j_call(SMB2J_GetEnemyOffscreenBits, (uint8_t)slot);
            smb2j_call(SMB2J_RelativeEnemyPosition, (uint8_t)slot);
            smb2j_call(SMB2J_GetEnemyBoundBox, (uint8_t)slot);
        }
        POKE(RAM_ObjectOffset, 1);
        smb2j_call(SMB2J_EnemiesCollision, 1);
        if (RAM(RAM_Enemy_CollisionBits) & 0x40) *contact |= mask;
        else *contact &= (uint8_t)~mask;
        uint8_t ac = a->state[F_COLL], bc = b->state[F_COLL];
        pull(a, 0);
        pull(b, 1);
        a->state[F_COLL] = ac;
        b->state[F_COLL] = bc;
        end();
        if (a->native_slot >= 0) push(a, a->native_slot);
        if (b->native_slot >= 0) push(b, b->native_slot);
    }
}

/* ---- hook sites (game.toml; runner/cyc/cyc_hooks.h). Returning 1 handles
 * the routine: the cycle scheduler returns from it as its RTS would. ---- */

static int spawn_hook(uint16_t addr) {
    (void)addr;
    if (!s_enabled || s_virtual || !gameplay() || !ensure()) return 0;
    int slot = smb2j_reg_x();
    if (slot >= 5 || !managed(RAM((uint16_t)(RAM_Enemy_ID + slot)))) return 0;
    int ofs = RAM(RAM_EnemyDataOffset), n = -1;
    /* Group initialization calls this repeatedly at the same data offset. */
    int code = PRG((uint16_t)(s.enemy_data + ofs + 1)) & 63, grouped = code >= 0x37 && code <= 0x3e;
    int member = grouped ? (live_x(slot) - (RAM(RAM_ScreenRight_PageLoc) << 8 | RAM(RAM_ScreenRight_X_Pos))) / 24 : 0;
    for (int i = 0; i < s.count; i++)
        if (s.actor[i].offset == ofs && s.actor[i].kind == RAM((uint16_t)(RAM_Enemy_ID + slot)) &&
            s.actor[i].member == member && (grouped || abs((int)s.actor[i].spawn_x - live_x(slot)) <= 48)) {
            n = i;
            break;
        }
    if (n < 0) return 0;
    Actor *a = &s.actor[n];
    if (s_mode == SMB_ENEMIES_CLASSIC && !a->active && !a->dead) {
        s.owner[slot] = (int16_t)n;
        a->native_slot = (int16_t)slot;
        a->active = a->loaded = 1;
        return 0; /* original activation/initialization, including its cycles */
    }
    if (!a->loaded && !a->dead) initialize(a);
    a->active = 1;
    if (!a->dead && a->native_slot < 0 && actor_x(a) >= camera() - 24 && actor_x(a) <= camera() + 304) {
        a->state[F_COLL] = 0;
        clear_collision_bits(slot);
        push(a, slot);
        s.owner[slot] = (int16_t)n;
        a->native_slot = (int16_t)slot;
        s.transfers++;
    } else {
        /* The parser must consume a resident/dead record, without creating a
         * second object. Its success flag is retired before another game tick. */
        POKE((uint16_t)(RAM_Enemy_Flag + slot), 1);
        s.owner[slot] = -2;
    }
    return 1;
}

static int gfx_hook(uint16_t addr) {
    (void)addr;
    if (!s_enabled || s_virtual || !gameplay()) return 0;
    int slot = smb2j_reg_x();
    if (slot >= 5) return 0;
    int n = s.owner[slot];
    uint8_t id = RAM((uint16_t)(RAM_Enemy_ID + slot));
    /* Initial screen construction can create a plant before gameplay starts.
     * Adopt that live object before considering any preview of the same pipe. */
    if (n < 0 && plant(id) && RAM((uint16_t)(RAM_Enemy_Flag + slot)) && ensure()) {
        for (int i = 0; i < s.count; i++)
            if (s.actor[i].offset == 255 && s.actor[i].kind == id && s.actor[i].spawn_x == live_x(slot) &&
                s.actor[i].native_slot < 0) {
                n = i;
                Actor *a = &s.actor[n];
                a->loaded = a->active = 1;
                a->dead = 0;
                a->native_slot = (int16_t)slot;
                s.owner[slot] = (int16_t)n;
                pull(a, slot);
                break;
            }
    }
    /* Adopt native platforms already present when loading a machine-only
     * checkpoint. Multiple lifts may share X; claim a distinct resident for
     * each live slot, preserving its current phase instead of reinitializing. */
    if (n < 0 && platform(id) && RAM((uint16_t)(RAM_Enemy_Flag + slot)) && ensure()) {
        int best = 65536;
        for (int i = 0; i < s.count; i++) {
            Actor *a = &s.actor[i];
            if (a->kind != id || a->native_slot >= 0 || a->dead) continue;
            int shifted = (a->kind == 0x26 || a->kind == 0x27 || a->kind >= 0x2b) ? 12 : 0;
            int dx = abs((int)a->spawn_x + shifted - live_x(slot));
            int dy = abs((int)(a->loaded ? a->state[F_Y] : a->spawn_y) - RAM((uint16_t)(RAM_Enemy_Y_Position + slot)));
            if (dy > 128) dy = 256 - dy;
            if (dx <= 48 && dx * 256 + dy < best) { n = i; best = dx * 256 + dy; }
        }
        if (n >= 0) {
            Actor *a = &s.actor[n];
            a->loaded = a->active = 1;
            a->native_slot = (int16_t)slot;
            s.owner[slot] = (int16_t)n;
            pull(a, slot);
        }
    }
    if (n >= 0) {
        Actor current = s.actor[n];
        pull(&current, slot);
        int oam_base = RAM((uint16_t)(RAM_Enemy_SprDataOffset + slot)) / 4;
        if (begin(&current)) { graphics(&s.next[n], 1, oam_base); end(); }
    }
    return 0;
}

static int plant_hook(uint16_t addr) {
    (void)addr;
    if (!s_enabled || s_virtual || smb_ws_world_busy() || !gameplay() || !ensure()) return 0;
    int slot = smb2j_reg_x();
    if (slot >= 5) return 0;
    int n = -1;
    uint8_t id = RAM((uint16_t)(RAM_Enemy_ID + slot));
    for (int i = 0; i < s.count; i++)
        if (s.actor[i].offset == 255 && s.actor[i].kind == id && s.actor[i].spawn_x == live_x(slot)) { n = i; break; }
    if (n < 0) return 0;
    Actor *a = &s.actor[n];
    if (s_mode == SMB_ENEMIES_CLASSIC && !a->active && !a->dead) {
        s.owner[slot] = (int16_t)n;
        a->native_slot = (int16_t)slot;
        a->active = a->loaded = 1;
        return 0;
    }
    if (!a->loaded && !a->dead) initialize(a);
    /* The real initializer's work stays done - its register/flag result and
     * the world-dependent attribute patches it writes - before its slot goes
     * to an existing resident (or a duplicate retires). */
    CycModRegs r;
    cyc_mod_regs(&r);
    s_virtual = 1;
    bool ran = cyc_mod_call_commit(SMB2J_InitPiranhaPlant, &r);
    s_virtual = 0;
    if (!ran) return 0;   /* reported; the original runs */
    a->active = 1;
    if (!a->dead && a->native_slot < 0 && a->loaded) {
        push(a, slot);
        s.owner[slot] = (int16_t)n;
        a->native_slot = (int16_t)slot;
        s.transfers++;
    } else {
        POKE((uint16_t)(RAM_Enemy_Flag + slot), 0);
        s.owner[slot] = -1;
    }
    return 1;
}

static int flag_hook(uint16_t addr) {
    (void)addr;
    if (!s_enabled || s_virtual || smb_ws_world_busy() || !gameplay() || smb2j_reg_x() != 5 ||
        !RAM(RAM_Enemy_Flag + 5) || RAM(RAM_Enemy_ID + 5) != 0x30 || smb_ws_world_flag_x() != live_x(5))
        return 0;
    Actor flag;
    memset(&flag, 0, sizeof flag);
    pull(&flag, 5);
    flag_graphics(&s.flag_next, &flag, 1, RAM(RAM_Enemy_SprDataOffset + 5) / 4);
    return 0;
}

static int cull_hook(uint16_t addr) {
    (void)addr;
    if (!s_enabled || s_virtual || !gameplay()) return 0;
    int slot = smb2j_reg_x();
    if (slot >= 5 || s.owner[slot] < 0) return 0;
    int x = live_x(slot), cam = camera();
    return x < cam || x > cam + 304;
}

void smb_ws_actors_begin_frame(void) {
    if (!s_enabled) return;
    s_render_wide = cyc_video_width() > 256;
    memcpy(s.display, s.next, sizeof s.display);
    memset(s.next, 0, sizeof s.next);
    s.flag_display = s.flag_next;
    memset(&s.flag_next, 0, sizeof s.flag_next);
    s.camera = camera();
}

void smb_ws_actors_update(void) {
    if (!s_enabled || !gameplay() || !ensure()) return;
    int cam = camera(), left, right;
    smb2j_ws_actor_range(cam, cyc_video_width(), PAD, &left, &right);
    uint8_t native_ticked[MAX_ACTORS] = { 0 };
    for (int slot = 0; slot < 5; slot++) {
        int n = s.owner[slot];
        if (n == -2) { POKE((uint16_t)(RAM_Enemy_Flag + slot), 0); s.owner[slot] = -1; continue; }
        if (n < 0) continue;
        native_ticked[n] = 1;
        Actor *a = &s.actor[n];
        pull(a, slot);
        if (!a->state[F_FLAG]) { a->dead = 1; a->native_slot = -1; s.owner[slot] = -1; continue; }
        int x = actor_x(a);
        if (x < cam - 24 || x > cam + 304) detach(slot);
    }
    for (int n = 0; n < s.count; n++) {
        Actor *a = &s.actor[n];
        if (a->dead || a->native_slot >= 0) continue;
        if (!a->loaded) {
            if (a->spawn_x < left || a->spawn_x > right) continue;
            initialize(a);
            if (!a->loaded) continue;
            a->active = s_mode == SMB_ENEMIES_VIEWPORT;
        }
        int x = actor_x(a);
        if (a->active && (x < left - 32 || x > right + 32)) { a->dead = 1; continue; }
        if (native_ticked[n]) continue; /* already advanced by this frame's native routine */
        uint8_t paused = RAM(RAM_GamePauseStatus) & 1;
        if (!begin(a)) continue;
        int run = a->active && !paused;
        if (run && !RAM(RAM_TimerControl)) {
            if (RAM(RAM_EnemyFrameTimer)) POKE(RAM_EnemyFrameTimer, (uint8_t)(RAM(RAM_EnemyFrameTimer) - 1));
            if (RAM(RAM_IntervalTimerControl) == 0x14 && RAM(RAM_EnemyIntervalTimer))
                POKE(RAM_EnemyIntervalTimer, (uint8_t)(RAM(RAM_EnemyIntervalTimer) - 1));
        }
        if (!a->active) POKE(RAM_FrameCounter, 0);
        POKE(RAM_Enemy_SprAttrib, 0);
        if (platform(a->kind)) {
            /* Native platform collision remains in its interaction slot.
             * A detached platform has no rider. Large platforms move before
             * drawing; small paired lifts draw before their shared movement. */
            POKE(RAM_PlatformCollisionFlag, a->kind >= 0x2b ? 0 : 0xff);
            if (run && !RAM(RAM_TimerControl) && a->kind < 0x2b) {
                smb2j_call(SMB2J_LargePlatformSubroutines, 0);
                s.updates++;
            }
            graphics(&s.next[n], 0, 64);
            if (run && !RAM(RAM_TimerControl) && a->kind >= 0x2b) {
                smb2j_call(SMB2J_MoveLiftPlatforms, 0);
                s.updates++;
            }
        } else {
            graphics(&s.next[n], 0, 64);
        }
        if (!platform(a->kind) && run && RAM(RAM_Enemy_Flag)) {
            smb2j_call(SMB2J_EnemyToBGCollisionDet, 0);
            if (!RAM(RAM_TimerControl)) { smb2j_call(SMB2J_EnemyMovementSubs, 0); s.updates++; }
        }
        pull(a, 0);
        end();
        if (!a->state[F_FLAG]) { a->dead = 1; continue; }
        x = actor_x(a);
        if (a->active && x >= cam - 24 && x <= cam + 288) promote(n);
    }
    collide_residents();
    if (!s.flag_next.count) {
        int x = smb_ws_world_flag_x();
        if (x >= 0 && x >= left - 16 && x <= right) {
            Actor flag;
            memset(&flag, 0, sizeof flag);
            flag.state[F_FLAG] = 1;
            flag.state[F_ID] = 0x30;
            flag.state[F_PAGE] = (uint8_t)(x >> 8);
            flag.state[F_X] = (uint8_t)x;
            flag.state[F_YH] = 1;
            flag.state[F_Y] = 0x30;
            /* Before native loading, preview only the authored flag. Once
             * loaded, the flag hook supplies its real animation and score. */
            if (RAM(RAM_Enemy_ID + 5) != 0x30 || live_x(5) != x) flag_graphics(&s.flag_next, &flag, 0, 64);
        }
    }
}

void smb_ws_actors_draw(uint32_t *out, int width, int height, int native_x0, int render_camera, const uint8_t *opaque) {
    if (!s_enabled || !gameplay() || !(cyc_render_line_mask(120) & 0x10)) return;
    uint16_t pattern = cyc_render_line_sprite_table(120);
    const uint8_t *oam = cyc_render_oam();
    /* Native OAM owners draw first when they have higher sprite priority.
     * Preserve their opaque pixels when replacing an enemy behind them. */
    static uint8_t native_priority[256 * 240];
    memset(native_priority, 255, sizeof native_priority);
    for (int slot = 63; slot >= 0; slot--) {
        const uint8_t *o = oam + slot * 4;
        if (o[0] >= 0xef) continue;
        for (int y = 0; y < 8; y++) {
            uint8_t lo = cyc_render_chr((uint16_t)(pattern + o[1] * 16 + ((o[2] & 128) ? 7 - y : y)));
            uint8_t hi = cyc_render_chr((uint16_t)(pattern + o[1] * 16 + ((o[2] & 128) ? 7 - y : y) + 8));
            for (int x = 0; x < 8; x++) {
                int dx = o[3] + x, dy = o[0] + 1 + y;
                if (dx >= 256 || dy >= 240 || (dx < 8 && !(cyc_render_line_mask(dy) & 4))) continue;
                int bit = (o[2] & 64) ? x : 7 - x;
                if (((lo >> bit) & 1) | (((hi >> bit) & 1) << 1)) native_priority[dy * 256 + dx] = (uint8_t)slot;
            }
        }
    }
    for (int n = s.count; n >= 0; n--) {
        const Packet *p = n == s.count ? &s.flag_display : &s.display[n];
        for (int i = p->count - 1; i >= 0; i--) {
            const Sprite *v = &p->sprite[i];
            for (int y = 0; y < 8; y++) {
                int dy = v->y + y;
                if (dy < SMB2J_HUD_ROWS || dy >= height) continue;
                int row = (v->attr & 128) ? 7 - y : y;
                uint8_t lo = cyc_render_chr((uint16_t)(pattern + v->tile * 16 + row));
                uint8_t hi = cyc_render_chr((uint16_t)(pattern + v->tile * 16 + row + 8));
                for (int x = 0; x < 8; x++) {
                    int dx = v->x - render_camera + native_x0 + x;
                    if (dx < 0 || dx >= width) continue;
                    if (dx >= native_x0 && dx < native_x0 + 256 &&
                        native_priority[dy * 256 + dx - native_x0] < (p->native ? p->oam_base : 64))
                        continue;
                    int bit = (v->attr & 64) ? x : 7 - x;
                    int color = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                    if (!color || ((v->attr & 32) && opaque[dy * width + dx])) continue;
                    out[dy * width + dx] = cyc_render_color(16 + (v->attr & 3) * 4 + color);
                }
            }
        }
    }
}

void smb_ws_actors_configure(int enabled, SmbEnemyMode mode) {
    int active = enabled && mode != SMB_ENEMIES_NATIVE;
    if (active != s_enabled || mode != s_mode) smb_ws_actors_reset();
    s_enabled = active;
    s_mode = mode;
    nes_mod_set_function_hook_enabled("smb2j.widescreen.enemy-init", active);
    nes_mod_set_function_hook_enabled("smb2j.widescreen.enemy-gfx", active);
    nes_mod_set_function_hook_enabled("smb2j.widescreen.enemy-cull", active);
    nes_mod_set_function_hook_enabled("smb2j.widescreen.flag-gfx", active);
    nes_mod_set_function_hook_enabled("smb2j.widescreen.plant-init", active);
    nes_mod_set_function_hook_enabled("smb2j.widescreen.large-platform-gfx", active);
    nes_mod_set_function_hook_enabled("smb2j.widescreen.small-platform-gfx", active);
}

int smb_ws_actors_json(char *buf, int cap) {
    int loaded = 0, native = 0, active = 0, sprites = 0;
    for (int i = 0; i < s.count; i++) if (s.actor[i].loaded && !s.actor[i].dead) {
        loaded++;
        native += s.actor[i].native_slot >= 0;
        active += s.actor[i].active;
        sprites += s.display[i].count;
    }
    int len = snprintf(buf, (size_t)cap,
                       "\"enemy_policy\":%d,\"authored\":%d,\"loaded\":%d,\"active\":%d,\"native\":%d,"
                       "\"sprites\":%d,\"updates\":%llu,\"transfers\":%llu,\"loads\":%llu,\"flag\":{\"world_x\":%d,"
                       "\"sprites\":%d,\"native\":%d,\"x\":%d,\"y\":%d},\"enemies\":[",
                       s_mode, s.count, loaded, active, native, sprites, (unsigned long long)s.updates,
                       (unsigned long long)s.transfers, (unsigned long long)s.loads, smb_ws_world_flag_x(),
                       s.flag_display.count, s.flag_display.native,
                       s.flag_display.count ? s.flag_display.sprite[0].x : -1,
                       s.flag_display.count ? s.flag_display.sprite[0].y : -1);
    int comma = 0;
    for (int i = 0; i < s.count && len < cap - 200; i++) if (s.actor[i].loaded) {
        const Actor *a = &s.actor[i];
        len += snprintf(buf + len, (size_t)(cap - len),
                        "%s{\"record\":%d,\"member\":%d,\"kind\":%d,\"spawn_x\":%d,\"x\":%d,\"y\":%d,\"active\":%d,"
                        "\"dead\":%d,\"slot\":%d,\"sprites\":%d,\"sx\":%d}",
                        comma++ ? "," : "", a->offset, a->member, a->kind, a->spawn_x, actor_x(a),
                        a->state[F_Y] + (a->state[F_YH] - 1) * 256, a->active, a->dead, a->native_slot,
                        s.display[i].count, s.display[i].count ? s.display[i].sprite[0].x : -1);
    }
    len += snprintf(buf + len, (size_t)(cap - len), "]");
    return len;
}

/* ---- TCP (window builds) ---- */
#ifdef CYC_WITH_SDL
static void tcp_flag(int id, const char *line) {
    (void)line;
    char f[200];
    snprintf(f, sizeof f, "\"world_x\":%d,\"sprites\":%d,\"native\":%d,\"x\":%d,\"y\":%d", smb_ws_world_flag_x(),
             s.flag_display.count, s.flag_display.native, s.flag_display.count ? s.flag_display.sprite[0].x : -1,
             s.flag_display.count ? s.flag_display.sprite[0].y : -1);
    cyc_tcp_ok(id, f);
}

static void tcp_enemies(int id, const char *line) {
    (void)line;
    static char buf[32000];
    int len = snprintf(buf, sizeof buf, "\"enabled\":%d,\"camera\":%d,", s_enabled, s.camera);
    smb_ws_actors_json(buf + len, (int)sizeof buf - len);
    cyc_tcp_ok(id, buf);
}

void smb_ws_actors_tcp_setup(void) {
    cyc_tcp_register("smb_ws_enemies", "widescreen residents: policy, counts, each actor", tcp_enemies);
    cyc_tcp_register("smb_ws_flag", "widescreen goal flag: world X, packet", tcp_flag);
}
#else
void smb_ws_actors_tcp_setup(void) {}
#endif

/* ---- save states (runner/include/mod_savestate.h) ---- */
static int save(uint8_t *data, int cap) {
    if (cap < (int)sizeof s) return -1;
    memcpy(data, &s, sizeof s);
    return sizeof s;
}
static int validate(const uint8_t *data, int len) {
    uint32_t version;
    if (!len) return 1;
    if (len != (int)sizeof s) return 0;
    memcpy(&version, data, sizeof version);
    if (version != ACTORS_VERSION) return 0;
    static Actors candidate;
    memcpy(&candidate, data, sizeof candidate);
    if (candidate.count > MAX_ACTORS) return 0;
    for (int i = 0; i < 5; i++) if (candidate.owner[i] < -2 || candidate.owner[i] >= candidate.count) return 0;
    for (int i = 0; i < candidate.count; i++)
        if (candidate.display[i].count > SPRITES || candidate.next[i].count > SPRITES) return 0;
    return candidate.flag_display.count <= SPRITES && candidate.flag_next.count <= SPRITES;
}
static int load(const uint8_t *data, int len) {
    if (!len) { smb_ws_actors_reset(); return 1; }
    if (!validate(data, len)) return 0;
    memcpy(&s, data, sizeof s);
    return 1;
}

NES_MOD_CONSTRUCTOR(register_smb2j_enemies) {
    if (!nes_mod_register_function_entry_plugin("smb2j.widescreen.enemy-init", SMB2J_CheckpointEnemyID, spawn_hook) ||
        !nes_mod_register_function_entry_plugin("smb2j.widescreen.enemy-gfx", SMB2J_EnemyGfxHandler, gfx_hook) ||
        !nes_mod_register_function_entry_plugin("smb2j.widescreen.enemy-cull", SMB2J_OffscreenBoundsCheck, cull_hook) ||
        !nes_mod_register_function_entry_plugin("smb2j.widescreen.flag-gfx", SMB2J_FlagpoleGfxHandler, flag_hook) ||
        !nes_mod_register_function_entry_plugin("smb2j.widescreen.plant-init", SMB2J_InitPiranhaPlant, plant_hook) ||
        !nes_mod_register_function_entry_plugin("smb2j.widescreen.large-platform-gfx", SMB2J_DrawLargePlatform, gfx_hook) ||
        !nes_mod_register_function_entry_plugin("smb2j.widescreen.small-platform-gfx", SMB2J_DrawSmallPlatform, gfx_hook) ||
        !nes_mod_register_savestate_hook("smb2j.widescreen.actors", save, load) ||
        !nes_mod_register_savestate_validator("smb2j.widescreen.actors", validate))
        fprintf(stderr, "[Widescreen] Failed to register enemy residents\n");
}
