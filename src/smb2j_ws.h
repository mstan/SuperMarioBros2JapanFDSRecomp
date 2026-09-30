/*
 * smb2j_ws.h - Super Mario Bros. 2 (Japan) custom widescreen, shared parts.
 *
 * A port of SuperMarioBrosRecomp's widescreen mod (docs/WIDESCREEN.md) to the
 * cycle backend: the program's own routines build a world cache and simulate
 * residents on an isolated machine (runner/cyc/cyc_mod.h), hook sites observe
 * the original routines (game.toml, runner/cyc/cyc_hooks.h), and a compositor
 * (runner/cyc/cyc_render.h) presents the wider picture at cyc_video's width.
 * Addresses are SMB2J's (docs/SMB2J_SYMBOL_MAP.md; the reference disassembly
 * assembles to this disk image byte for byte).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "cyc_core.h"
#include "cyc_mod.h"

/* ---- routines (SM2MAIN, resident at $6000-$C2B3) ---- */
enum {
    SMB2J_RenderAreaGraphics       = 0x678B,
    SMB2J_MetatileGraphics_Low     = 0x69E5,
    SMB2J_MetatileGraphics_High    = 0x69E9,
    SMB2J_InitializeArea           = 0x6E39,
    SMB2J_AreaParserCore           = 0x720D,
    SMB2J_CheckpointEnemyID        = 0x8E50,
    SMB2J_InitPiranhaPlant         = 0x9398,
    SMB2J_EnemyMovementSubs        = 0x953A,
    SMB2J_LargePlatformSubroutines = 0x95B7,
    SMB2J_MoveLiftPlatforms        = 0xA295,
    SMB2J_OffscreenBoundsCheck     = 0xA2B4,
    SMB2J_EnemiesCollision         = 0xA69C,
    SMB2J_EnemyToBGCollisionDet    = 0xAC4A,
    SMB2J_GetEnemyBoundBox         = 0xAEE1,
    SMB2J_FlagpoleGfxHandler       = 0xB1F1,
    SMB2J_DrawLargePlatform        = 0xB26E,
    SMB2J_EnemyGfxHandler          = 0xB52C,
    SMB2J_DrawSmallPlatform        = 0xBA41,
    SMB2J_RelativeEnemyPosition    = 0xBE37,
    SMB2J_GetEnemyOffscreenBits    = 0xBE94,
};

/* ---- RAM (the same addresses as SMB1 unless noted) ---- */
enum {
    RAM_ObjectOffset         = 0x0008,
    RAM_FrameCounter         = 0x0009,
    RAM_Enemy_Flag           = 0x000F,
    RAM_Enemy_ID             = 0x0016,
    RAM_Player_PageLoc       = 0x006D,
    RAM_Enemy_PageLoc        = 0x006E,
    RAM_Player_X_Position    = 0x0086,
    RAM_Enemy_X_Position     = 0x0087,
    RAM_Enemy_Y_Position     = 0x00CF,
    RAM_AreaData             = 0x00E7,
    RAM_EnemyData            = 0x00E9,
    RAM_FlagpoleFNum_Y_Pos   = 0x010D,
    RAM_BowserGfxFlag        = 0x036A,
    RAM_PlatformCollisionFlag= 0x03A2,
    RAM_Enemy_SprAttrib      = 0x03C5,
    RAM_Enemy_CollisionBits  = 0x0491,
    RAM_Block_Buffer_1       = 0x0500,
    RAM_BlockBufferColumnPos = 0x06A0,
    RAM_MetatileBuffer       = 0x06A1,
    RAM_SecondaryHardMode    = 0x06CC,
    RAM_Enemy_SprDataOffset  = 0x06E5,
    RAM_FlagpoleCollisionYPos= 0x070F,
    RAM_ScreenLeft_PageLoc   = 0x071A,
    RAM_ScreenRight_PageLoc  = 0x071B,
    RAM_ScreenLeft_X_Pos     = 0x071C,
    RAM_ScreenRight_X_Pos    = 0x071D,
    RAM_AreaParserTaskNum    = 0x071F,
    RAM_CurrentNTAddr_High   = 0x0720,
    RAM_CurrentNTAddr_Low    = 0x0721,
    RAM_ScrollLock           = 0x0723,
    RAM_CurrentPageLoc       = 0x0725,
    RAM_CurrentColumnPos     = 0x0726,
    RAM_TerrainControl       = 0x0727,
    RAM_AreaDataOffset       = 0x072C,
    RAM_AreaObjectLength     = 0x0730,
    RAM_AreaStyle            = 0x0733,
    RAM_EnemyDataOffset      = 0x0739,
    RAM_ForegroundScenery    = 0x0741,
    RAM_BackgroundScenery    = 0x0742,
    RAM_CloudTypeOverride    = 0x0743,
    RAM_BackgroundColorCtrl  = 0x0744,
    RAM_TimerControl         = 0x0747,
    RAM_AreaType             = 0x074E,
    RAM_AreaNumber           = 0x0760,
    RAM_LevelNumber          = 0x075C,
    RAM_WorldNumber          = 0x075F,
    RAM_PrimaryHardMode      = 0x076A,
    RAM_OperMode             = 0x0770,
    RAM_OperMode_Task        = 0x0772,
    RAM_GamePauseStatus      = 0x0776,
    RAM_IntervalTimerControl = 0x077F,
    RAM_EnemyFrameTimer      = 0x078A,
    RAM_EnemyIntervalTimer   = 0x0796,
    RAM_FileListNumber       = 0x07F7,   /* SMB2J: the disk state (which overlay is loaded) */
    RAM_HardWorldFlag        = 0x07FB,   /* SMB2J: worlds A-D (WorldNumber 0-3 again) */
};

enum {
    SMB2J_WORLD8 = 7,             /* WorldNumber of world 8 (world D sets it too) */
    SMB2J_GAME_ENGINE_TASK = 4,   /* GameModeSubs' GameCoreRoutine */
    SMB2J_HUD_ROWS = 32,          /* the status bar, above the FDS IRQ split */
};

/* The machine's CPU RAM (reads; writes go through cyc_mod_poke). */
extern const uint8_t *smb2j_ram;
static inline uint8_t RAM(uint16_t a) { return smb2j_ram[a & 0x7FF]; }
static inline void    POKE(uint16_t a, uint8_t v) { cyc_mod_poke(a, v); }
/* PRG RAM / BIOS as the machine holds them now. */
static inline uint8_t PRG(uint16_t a) { return cyc_mod_peek(a); }

/* An isolated call of a program routine with X (and S at $FD); false when it
 * did not return (reported by cyc_mod.c). */
bool smb2j_call(uint16_t routine, uint8_t x);
/* The registers at the current hook site. */
uint8_t smb2j_reg_x(void);

/* ---- the mod (smb2j_ws.c) ---- */
typedef enum { SMB_ENEMIES_NATIVE, SMB_ENEMIES_CLASSIC, SMB_ENEMIES_VIEWPORT } SmbEnemyMode;
void smb2j_ws_set_mod_enabled(int enabled);
void smb2j_ws_configure(const char *aspect, const char *hud, const char *enemies);
void smb2j_ws_set_camera(const char *camera);
int  smb2j_ws_view_left(int native_camera, int width);
void smb2j_ws_actor_range(int native_camera, int width, int pad, int *left, int *right);
/* Area scenes the world cache renders: the game engine and the castle scenes
 * of victory mode (not world 8's disk loads, final room or ending). */
bool smb2j_ws_world_scene(void);
/* The game engine proper (residents move only here). */
bool smb2j_ws_game_engine(void);
