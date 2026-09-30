/* SMB2J's room compiler feeds a host-owned world cache, never wider PPU RAM.
 * The original AreaParserCore runs on an isolated machine (cyc_mod.h) outside
 * rendering: no guest time advances and everything it changed - CPU RAM, the
 * PRG RAM it patches, the CPU - is restored afterwards. Live streamed columns
 * validate the cache and carry later block/coin edits into it.
 *
 * SMB2J differences from SMB1 (docs/SMB2J_SYMBOL_MAP.md): area and enemy
 * streams live in PRG RAM ($6002-$DFFF), in an overlay the disk state
 * (FileListNumber) selects, and worlds A-D reuse WorldNumber 0-3 - the cache
 * is keyed on both; upside-down piranha plants (id $04) come out of the area
 * parser like ordinary ones. */
#include "smb2j_ws_world.h"
#include "smb2j_ws.h"
#include "cyc_render.h"

#include <string.h>

SmbWsWorld g_smb_ws_world;
static int s_decoding;

static unsigned area_data(void) { return RAM(RAM_AreaData) | RAM(RAM_AreaData + 1) << 8; }

static uint8_t tile_for(uint8_t meta, int x, int y) {
    int bank = meta >> 6;
    unsigned table = PRG((uint16_t)(SMB2J_MetatileGraphics_Low + bank)) |
                     PRG((uint16_t)(SMB2J_MetatileGraphics_High + bank)) << 8;
    return PRG((uint16_t)(table + (meta & 63) * 4 + x * 2 + y));
}

void smb_ws_world_reset(void) {
    memset(&g_smb_ws_world, 0, sizeof g_smb_ws_world);
    g_smb_ws_world.version = SMB_WS_WORLD_VERSION;
    g_smb_ws_world.flag_x = -1;
    memset(g_smb_ws_world.block_world, 0xff, sizeof g_smb_ws_world.block_world);
    memset(g_smb_ws_world.nt_world, 0xff, sizeof g_smb_ws_world.nt_world);
    memset(g_smb_ws_world.pending_world, 0xff, sizeof g_smb_ws_world.pending_world);
    g_smb_ws_world.first_mismatch_column = -1;
}

int smb_ws_world_busy(void) { return s_decoding; }

static void decode(void) {
    SmbWsWorld *w = &g_smb_ws_world;
    unsigned data = area_data();
    uint8_t header0 = PRG((uint16_t)(data - 2)), header1 = PRG((uint16_t)(data - 1));
    uint8_t area_type = RAM(RAM_AreaType), world = RAM(RAM_WorldNumber);
    uint8_t file_list = RAM(RAM_FileListNumber), hard_world = RAM(RAM_HardWorldFlag);
    smb_ws_world_reset();
    w->area_data = (uint16_t)data;
    w->area_type = area_type;
    w->world = world;
    w->file_list = file_list;
    w->hard_world = hard_world;
    if (!cyc_mod_isolate_begin()) return;
    s_decoding = 1;
    CycModStats before;
    cyc_mod_stats(&before);
    /* Match InitializeArea's cleared working set and the area header
     * (GetAreaDataAddrs). AreaPointer can already refer to the next pipe
     * destination; the live AreaData pointer identifies the area actually
     * being rendered. */
    for (unsigned a = 0; a < 0x74C; ++a) POKE((uint16_t)a, 0);
    POKE(RAM_AreaData, (uint8_t)data);
    POKE(RAM_AreaData + 1, (uint8_t)(data >> 8));
    POKE(RAM_AreaObjectLength, 0xff);
    POKE(RAM_AreaObjectLength + 1, 0xff);
    POKE(RAM_AreaObjectLength + 2, 0xff);
    POKE(RAM_ForegroundScenery, (header0 & 7) < 4 ? header0 & 7 : 0);
    POKE(RAM_BackgroundColorCtrl, (header0 & 7) >= 4 ? header0 & 7 : 0);
    POKE(RAM_TerrainControl, header1 & 15);
    POKE(RAM_BackgroundScenery, (header1 >> 4) & 3);
    POKE(RAM_AreaStyle, header1 >> 6);
    if ((header1 >> 6) == 3) { POKE(RAM_CloudTypeOverride, 3); POKE(RAM_AreaStyle, 0); }
    int first_lock = -1, ok = 1;
    for (int col = 0; col < SMB_WS_META_COLUMNS && ok; col++) {
        POKE(RAM_CurrentPageLoc, (uint8_t)(col >> 4));
        POKE(RAM_CurrentColumnPos, (uint8_t)(col & 15));
        POKE(RAM_BlockBufferColumnPos, (uint8_t)(col & 31));
        /* Area objects create plants directly, outside EnemyData. Capacity
         * in this private parser must not discard later pipe seeds. */
        for (int slot = 0; slot < 5; ++slot) POKE((uint16_t)(RAM_Enemy_Flag + slot), 0);
        ok = smb2j_call(SMB2J_AreaParserCore, 0);
        if (first_lock < 0 && RAM(RAM_ScrollLock)) first_lock = col;
        /* $FD ends the object stream, but a buffered castle/pipe can still
         * have columns left to render. The cache continues for validation;
         * presentation stops at the last authored page, not its 8192px cap. */
        if (!w->area_end && PRG((uint16_t)(data + RAM(RAM_AreaDataOffset))) == 0xfd &&
            (RAM(RAM_AreaObjectLength) & RAM(RAM_AreaObjectLength + 1) & RAM(RAM_AreaObjectLength + 2) & 0x80))
            w->area_end = (uint16_t)(((col + 16) / 16) * 256);
        for (int slot = 0; slot < 5; slot++) {
            uint8_t id = RAM((uint16_t)(RAM_Enemy_ID + slot));
            if (!RAM((uint16_t)(RAM_Enemy_Flag + slot)) || (id != 0x0d && id != 0x04)) continue;
            if (w->plant_count < SMB_WS_MAX_PLANTS) {
                SmbWsPlant *p = &w->plants[w->plant_count++];
                p->x = (uint16_t)(RAM((uint16_t)(RAM_Enemy_PageLoc + slot)) << 8 | RAM((uint16_t)(RAM_Enemy_X_Position + slot)));
                p->y = RAM((uint16_t)(RAM_Enemy_Y_Position + slot));
                p->kind = id;
            }
        }
        /* the flagpole's flag is slot 5, id $30 */
        if (RAM(RAM_Enemy_Flag + 5) && RAM(RAM_Enemy_ID + 5) == 0x30 &&
            (RAM(RAM_Enemy_PageLoc + 5) << 8 | RAM(RAM_Enemy_X_Position + 5)) == col * 16 - 8)
            w->flag_x = (int16_t)(col * 16 - 8);
        for (int row = 0; row < 13; ++row) w->metatiles[col][row] = RAM((uint16_t)(RAM_MetatileBuffer + row));
        int block = RAM_Block_Buffer_1 + ((col & 16) ? 0xd0 : 0) + (col & 15);
        for (int row = 0; row < 13; row++) w->collision[col][row] = RAM((uint16_t)(block + row * 16));
        for (int row = 0; row < 26; row++) {
            uint8_t meta = RAM((uint16_t)(RAM_MetatileBuffer + row / 2));
            for (int x = 0; x < 2; x++) {
                w->tiles[col * 2 + x][row] = tile_for(meta, x, row & 1);
                w->palettes[col * 2 + x][row] = meta >> 6;
            }
        }
        w->decoded_columns++;
    }
    if (!w->area_end) w->area_end = SMB_WS_META_COLUMNS * 16;
    /* InitializeArea preloads 24 columns. A lock in that initial set means
     * the native view cannot scroll: the pipe intros and the bonus rooms.
     * Each entrance selects its own page; adjacent entries are separate
     * rooms. */
    w->fixed_rooms = first_lock >= 0 && first_lock < 24;
    CycModStats after;
    cyc_mod_stats(&after);
    w->decode_cycles = (uint32_t)(after.cycles - before.cycles);
    cyc_mod_isolate_end();
    s_decoding = 0;
    w->valid = (uint8_t)ok;
}

int smb_ws_world_ensure(void) {
    unsigned data = area_data();
    if (s_decoding || data < 0x6002 || data > 0xDFFF) return 0;
    SmbWsWorld *w = &g_smb_ws_world;
    if (!w->valid || w->area_data != data || w->area_type != RAM(RAM_AreaType) || w->world != RAM(RAM_WorldNumber) ||
        w->file_list != RAM(RAM_FileListNumber) || w->hard_world != RAM(RAM_HardWorldFlag))
        decode();
    return w->valid;
}

void smb_ws_world_observe_column(void) {
    if (!smb_ws_world_ensure()) return;
    SmbWsWorld *w = &g_smb_ws_world;
    int col = RAM(RAM_CurrentPageLoc) * 16 + RAM(RAM_CurrentColumnPos);
    int side = (RAM(RAM_AreaParserTaskNum) ^ 1) & 1;
    if (col >= SMB_WS_META_COLUMNS) return;
    /* Each 16-pixel column is submitted as two separate VRAM writes. */
    if (side == 0) {
        w->block_world[col & 31] = (int16_t)col;
        int mismatch = 0;
        for (int row = 0; row < 13; row++) {
            uint8_t actual = RAM((uint16_t)(RAM_MetatileBuffer + row));
            if (actual != w->metatiles[col][row]) {
                mismatch = 1;
                if (w->first_mismatch_column < 0) {
                    w->first_mismatch_column = col;
                    w->first_mismatch_row = row;
                    w->first_expected = w->metatiles[col][row];
                    w->first_actual = actual;
                }
            }
        }
        if (mismatch) w->mismatched_columns++;
        else w->verified_columns++;
    }
    int nt = (RAM(RAM_CurrentNTAddr_High) >> 2) & 1, tx = RAM(RAM_CurrentNTAddr_Low) & 31;
    w->pending_world[nt][tx] = (int16_t)(col * 2 + side);
    for (int row = 0; row < 26; row++)
        w->pending_tiles[nt][tx][row] = tile_for(RAM((uint16_t)(RAM_MetatileBuffer + row / 2)), side, row & 1);
    /* Attributes are uploaded later, in four-tile groups. Their old physical
     * contents do not belong to this column just because its tiles arrived. */
    for (int row = 0; row < 26; row++)
        w->palettes[col * 2 + side][row] = RAM((uint16_t)(RAM_MetatileBuffer + row / 2)) >> 6;
}

void smb_ws_world_update(void) {
    if (!smb_ws_world_ensure()) return;
    SmbWsWorld *w = &g_smb_ws_world;
    for (int c = 0; c < 32; c++) {
        int world = w->block_world[c];
        if (world < 0) continue;
        int block = RAM_Block_Buffer_1 + ((c & 16) ? 0xd0 : 0) + (c & 15);
        for (int row = 0; row < 13; row++) w->collision[world][row] = RAM((uint16_t)(block + row * 16));
    }
    for (int nt = 0; nt < 2; nt++) for (int tx = 0; tx < 32; tx++) {
        uint16_t base = (uint16_t)(0x2000 + nt * 0x400 + tx);
        if (w->pending_world[nt][tx] >= 0) {
            int matches = 1;
            for (int row = 0; row < 26; row++)
                if (cyc_render_nametable((uint16_t)(base + (row + 4) * 32)) != w->pending_tiles[nt][tx][row]) {
                    matches = 0;
                    break;
                }
            if (matches) {
                w->nt_world[nt][tx] = w->pending_world[nt][tx];
                w->pending_world[nt][tx] = -1;
            }
        }
        int world = w->nt_world[nt][tx];
        /* A pending replacement makes the old binding untrustworthy. */
        if (world < 0 || w->pending_world[nt][tx] >= 0) continue;
        for (int row = 0; row < 26; row++) w->tiles[world][row] = cyc_render_nametable((uint16_t)(base + (row + 4) * 32));
    }
}

int smb_ws_world_flag_x(void) { return g_smb_ws_world.valid ? g_smb_ws_world.flag_x : -1; }

void smb_ws_world_bounds(int native_camera, int *left, int *right) {
    const SmbWsWorld *w = &g_smb_ws_world;
    *left = w->fixed_rooms ? native_camera & ~255 : 0;
    *right = w->fixed_rooms ? *left + 256 : w->area_end;
    /* Native scripted movement can continue past the last authored object.
     * Always keep its full playfield visible, including the castle walk. */
    if (*right < native_camera + 256) *right = native_camera + 256;
}

int smb_ws_world_pixel(int world_x, int y, uint8_t *palette, uint8_t *tile) {
    if (!g_smb_ws_world.valid || world_x < 0 || world_x >= SMB_WS_TILE_COLUMNS * 8 || y < 32 || y >= 240) return 0;
    int col = world_x / 8, row = (y - 32) / 8;
    *tile = g_smb_ws_world.tiles[col][row];
    *palette = g_smb_ws_world.palettes[col][row];
    return 1;
}

void smb_ws_world_begin_column(void) {
    if (!s_decoding && g_smb_ws_world.valid)
        g_smb_ws_world.block_world[RAM(RAM_BlockBufferColumnPos) & 31] = -1;
}
