# Custom widescreen (experimental)

Enable **Widescreen (Experimental)** in the launcher's **Mods** screen, or in
the in-game menu's **Mods** section. The package is off by default; with it
off the game runs exactly as without it (same machine state, same picture),
and the disk image is never changed. It is a port of Super Mario Bros.'s
widescreen mod (SuperMarioBrosRecomp, docs/WIDESCREEN.md)
to this game and to nesrecomp's cycle backend.

The renderer extends terrain, ordinary authored enemies, both kinds of piranha
plant (upside-down ones included), independent moving platforms, the goal
flag and the wind's leaves across the viewport. Enemy movement can begin when
an enemy enters the wide view or keep the original 4:3 activation timing.

## Presentation

Choose **Fit window**, **16:9**, **21:9** or **32:9**. Fit follows the
window's drawable aspect during play, clamped between the native aspect and
32:9. The status bar can stay centered or sit at the screen edges.

**Camera: Anchor at area edges** (the default) stops the wide view at the
start and end of the authored area; areas narrower than the view, the pipe
intros and the bonus rooms stay centered. **Keep native view centered**
keeps the native picture in the middle. Both keep the original game camera,
player boundaries and activation logic.

Square pixels throughout:

| Mode | Logical frame |
| --- | --- |
| Stock | 256 x 240 |
| 16:9 | 426 x 240 |
| 21:9 | 560 x 240 |
| 32:9 | 854 x 240 |
| Fit | 256–854 x 240, following the window |

Title, disk loading, world intermission, game over and the ending screens
(world 8's final room and messages) keep the centered native picture.

Developer overrides (they win over the saved Mods selection):

```powershell
.\SuperMarioBros2JapanFDSRecomp.exe --widescreen fit|16:9|21:9|32:9|off
.\SuperMarioBros2JapanFDSRecomp.exe --widescreen 32:9 --widescreen-enemies classic|viewport
.\SuperMarioBros2JapanFDSRecomp.exe --widescreen 32:9 --widescreen-camera edges|centered
.\SuperMarioBros2JapanFDSRecomp.exe --widescreen 32:9 --widescreen-hud edges|center
```

`--widescreen-enemies native` is a renderer diagnostic (no residents,
centered camera). `--widescreen-log FILE` writes one JSON line per frame
(mode, world cache, seams, residents, flag) for the probes; `--dev-start
W:L:A[:PAGE]` and `--dev-hold ADDR:VALUE` are test fixtures (below), never
player features.

## How it works

The game is SM2MAIN (resident at `$6000-$C2B3`) plus one data overlay at a
time: worlds 1-4 in SM2MAIN, 5-8 in SM2DATA2, the ending and world 9 in
SM2DATA3, A-D in SM2DATA4 (loaded over `$C2B4-$DFFF`). The ten hook sites
(`game.toml` `[[mod_function_hook]]`) are all in SM2MAIN's resident code and
carry a content key (length and CRC32 of their first 8 bytes, no game bytes
in the repository); the cycle backend fires them identically from compiled
code, compiled RAM views and its interpreter:

| Site | Routine | Use |
| --- | --- | --- |
| `smb2j.widescreen.area` | InitializeArea | a new or restarted area: drop the caches |
| `smb2j.widescreen.column` | RenderAreaGraphics | bind a streamed nametable column to its world column |
| `smb2j.widescreen.collision-column` | AreaParserCore | block-buffer column ownership |
| `smb2j.widescreen.enemy-init` | CheckpointEnemyID | resident handoff, no duplicates |
| `smb2j.widescreen.plant-init` | InitPiranhaPlant | plants ($0D, upside-down $04) as residents |
| `smb2j.widescreen.enemy-cull` | OffscreenBoundsCheck | residents retire through the host |
| `smb2j.widescreen.enemy-gfx` | EnemyGfxHandler | complete sprites before native culling |
| `smb2j.widescreen.large-platform-gfx` | DrawLargePlatform | platform packets |
| `smb2j.widescreen.small-platform-gfx` | DrawSmallPlatform | small lift packets |
| `smb2j.widescreen.flag-gfx` | FlagpoleGfxHandler | the goal flag and its score |

**World cache** (`src/smb2j_ws_world.c`). The original `AreaParserCore` runs
over the area's 512 metatile columns in an isolated guest call
(`runner/cyc/cyc_mod.h`): the whole machine, PRG RAM included, is restored
afterwards and the decode advances no guest time. The cache is keyed on the
live `AreaData` pointer, `FileListNumber` and `HardWorldFlag`, so the same
address in another overlay is another area. Streamed columns are checked
against the live parser (a physical nametable column is bound only after its
tile bytes match) and live tile edits (bricks, coins) update the cache.
Metatile graphics come from SMB2J's own tables (`$69E5`/`$69E9`), whose IDs
differ from SMB1's.

**Compositor** (`src/smb2j_ws.c`). The native picture stays authoritative
inside its 256 columns (sprites, priorities, transient tiles); the cache
fills the rest at the same world offset. The camera is the scroll the PPU
actually drew line 40 with (captured per line), below the status bar, whose
split is an FDS IRQ timer here rather than sprite 0. The status bar (32 rows)
is placed centered or split to the edges.

**Residents** (`src/smb2j_ws_actors.c`). Enemy records in world coordinates,
initialized, moved and drawn with the game's own routines in isolated calls;
near Mario a resident's complete state moves into a native slot for the
original interactions. `InitPiranhaPlant` and `EnemyGfxHandler` rewrite PRG
RAM (self-modifying code); isolation restores it, and the one committed call
(`InitPiranhaPlant`, entered by JMP from `SetupPiranhaPlant`) keeps only its
memory effects. Classic activation shows a frozen preview until the native
parser activates the enemy. The goal flag is previewed at its authored
position until the native flag loads, then follows its original lowering and
score; a frame in which the game did not run the flag's handler (the frame it
loads, a paused frame) keeps the picture it had.

**Wind** (worlds 5-8, A-D). SimulateWind draws twelve screen-space leaves
(tiles `$7A`/`$7B`, attributes `$41`), a field that repeats every 256 pixels;
the margins show the same field one and two screens over, drawn from the OAM
the picture used. The wind's push on Mario is the game's own logic, untouched.

**Save states.** The world cache and the residents are mod records in the
cycle backend's save states (`smb2j.widescreen.world`,
`smb2j.widescreen.actors`, versioned); mod settings stay in the Mods
selection and are not overwritten by loading a state.

## Remaining limits

- Bosses (Bowser), linked balance platforms (`$24`), frenzy spawners (flying
  Cheep Cheeps, Bullet Bills) and other special objects keep native spawning
  and culling; Hammer Bros' hammers and other generated projectiles are not
  resident.
- Mario's interactions use the five original enemy slots, assigned to nearby
  actors: not unlimited simulation.
- Status-bar sprites (leaves crossing the top rows) follow the native picture.

## Validation

`tests/widescreen_probe.py` boots the image in headless runs (the window
checks drive a hidden window over TCP with SDL's dummy video driver) and
reads the mod's per-frame log; nothing arms a capture.

```powershell
python tests/widescreen_probe.py --exe <mod build> --baseline <untouched stock build> `
    --window-exe <window build of the mod> --out <new directory>
```

Results on this branch (engine `feat/cyc-widescreen`):

| Check | Result |
| --- | --- |
| Stock: mod build with the package off, `--widescreen off`, packages installed but disabled | full-machine hash identical to the untouched stock build, 9000/9000 frames each (routes/play.txt) |
| Renderer isolation: 32:9 with `--widescreen-enemies native` | machine hash 9000/9000 identical; native playfield pixels inside the wide picture equal stock in 60/60 sampled frames |
| Seams and columns: play route at 16:9 / 21:9 / 32:9, level starts 1-2, 2-1, 4-3, 5-1, 7-1, A-1, D-2 | every world-scene frame aligned at both joins (e.g. 1763/1763 at 16:9), 26-70 streamed columns verified per run against the live parser, 0 mismatched |
| Enemies (1-1, 32:9) | 4 residents beyond the native screen; classic: none moved before native activation; viewport: 4/4 moving, none through the floor |
| Plants | 5-1 (upside-down $04 and $0D): 5 beyond the screen, frozen in classic, 5/5 moving in viewport; 1-1: 3, same |
| Platforms | 4-3: 3 beyond the screen; B-4: 4; frozen in classic, moving in viewport |
| Wind (7-1, 32:9) | leaf copies in the margins in 489 of 493 wind frames (up to 48); none in 1-1 |
| Flag (1-1 from page 10) | preview for 225 frames, all beyond the native screen; native takeover with 0 missing frames; lowered y 49 -> 172; 1-2 entry with 0 ghost-flag frames |
| World 9 (through world 8's ending) | ending pillarboxed (4599 frames, 0 composed); world 9 (SM2DATA3) 2326/2326 frames aligned, 40 columns verified, 0 mismatched |
| Save state (loaded in a new process) | machine hash 899/899, mod state 899/899, pictures 8/8 identical to the uninterrupted run |
| Presets | 426 / 560 / 854, off = 256; Fit from 11 drawable sizes equals the shared geometry (256-854) |
| Saved Mods selection (21:9, centered status bar and camera, classic) | applied: 560, both centered, classic |
| Live Fit (hidden window, TCP resize) | 8/8 sizes follow the drawable (256-854) |
| In-game menu Mods rows (hidden window) | enabling turns the widescreen on at once (Fit 426 at 1920x1080), Fit follows a resize (320 at 1024x768), Aspect row switches to 16:9 (426), selection saved in mods/state.toml |

Not covered by the probe: the launcher's Mods screen itself (recomp-ui,
shared with the other titles), bosses and the other native-only objects
above, and full playthroughs of every level.
