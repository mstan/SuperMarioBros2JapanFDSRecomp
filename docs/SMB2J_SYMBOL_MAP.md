# SMB1 (USA) to SMB2J (Japan, FDS) symbol map for the widescreen mod

Research artifact for porting `SuperMarioBrosRecomp` custom widescreen (`game_widescreen.c`, `smb_ws_world.c`, `smb_ws_actors.c`, `mods/widescreen_plugin.c`) to `SuperMarioBros2JapanFDSRecomp`. Machine-readable twin: `tools/smb2j_symbols.toml` (516 routine pairs, 80 RAM rows, tables, constants, dispatch maps, self-modifying writes, caveats). No game bytes are stored here; addresses, names and counts only.

## 1. How this was established

- Disk image parsed directly (8 files, table in section 2). Public reference: `threecreepio/smb2j-disassembly` (ca65 port of doppelganger). It was assembled with cc65 and the resulting disk side is **byte-identical** (65500 bytes, all 8 files) to the image in the game repo, so its labels and addresses are authoritative for this exact image.
- SMB1 side: `SuperMarioBrosRecomp/baserom.nes` (PRG CRC32 D445F698) + `symbols.sym`.
- Each SMB1 routine was matched to the SMB2J routine of the same label by recursive-descent disassembly of both (branches followed, JSR/JMP not), instruction sequences aligned with difflib; ROM-range absolute operands abstracted, RAM operands and immediates compared literally. The callee closure was then followed through JSR/JMP and JumpEngine tables (name-based pairing for tables, since SMB2J tables are longer). Name equality alone was never accepted: every pair has instruction counts (`instr.smb1/smb2j/matched`) in the TOML.
- RAM variables: every SMB1 operand address in matched instruction pairs mapped to the same SMB2J address (0 conflicts across the closure), plus identical equ values in both disassemblies.
- Result: **516 routine pairs (406 instruction-identical, 110 modified), 74+6 RAM rows resolved (0 changed), 0 unresolved among names the widescreen sources reference**; 2 SMB1 routines reached only through the closure have no SMB2J equivalent (section 8).

## 2. Disk files and overlays

| # | File | ID | Load | Size | End | Role | Residency |
|---|---|---|---|---|---|---|---|
| 0 | KYODAKU- | $00 | $2800 | $00E0 | $28DF | VRAM (nametable text) | not code |
| 1 | SM2CHAR1 | $01 | $0000 | $2000 | $1FFF | CHR | not code |
| 2 | SM2CHAR2 | $10 | $0760 | $0040 | $079F | CHR (ending) | not code |
| 3 | SM2MAIN | $05 | $6000 | $8000 | $DFFF | PRG: engine + worlds 1-4 (and 9 pointer tables) + menu | resident $6000-$C2B3 always; $C2B4-$D29E overlaid by SM2DATA4/2/3 |
| 4 | SM2DATA2 | $20 | $C470 | $0E2F | $D29E | PRG overlay: worlds 5-8 area/enemy data + wind + upside-down pipes | overlays $C470-$D29E (file list 1) |
| 5 | SM2DATA3 | $30 | $C5D0 | $0CCF | $D29E | PRG overlay: ending, world-9 areas, victory/final-room code | overlays $C5D0-$D29E (file list 2) |
| 6 | SM2DATA4 | $40 | $C2B4 | $0F4C | $D1FF | PRG overlay: worlds A-D area/enemy data + pointer tables + wind + upside-down pipes | overlays $C2B4-$D1FF (file list 3) |
| 7 | SM2SAVE | $0F | $D29F | $0001 | $D29F | PRG (1 byte at $D29F) | always |

Disk states, selected by `FileListNumber` ($07F7) via `LoadFiles`: 0 = worlds 1-4 (CHAR1+MAIN+SAVE), 1 = worlds 5-8 (DATA2), 2 = ending/world 9 (CHAR2+DATA3+SAVE), 3 = worlds A-D (DATA4, `HardWorldFlag` $07FB set). Loading a file overwrites only its own byte range, so:

- `$6000-$C2B3`: SM2MAIN, **always resident, never overlaid**. Every hook/direct PC below (all 19) lives here.
- `$C2B4-$D29E`: SM2MAIN copy of worlds 1-4 pointer tables, menu code and area/enemy streams, overlaid by DATA4 (`$C2B4-$D1FF`), DATA2 (`$C470-$D29E`), DATA3 (`$C5D0-$D29E`). Area data, enemy data, `GetAreaDataAddrs`/`FindAreaPointer` tables, wind and upside-down-pipe code are here.
- Area/enemy streams: main L_/E_ `$C260-$D21B`; DATA2 `$C5D0-$D242`; DATA3 `$CA80-$CBDA`; DATA4 `$C5D0-$CFDB`. SMB1 widescreen accepts `AreaData` in `$8002..$FF00`; SMB2J needs `$6002..$DFFF` and must read them from the PRG-RAM view (`mapper_peek_prg` is ROM-only).
- SMB2J-only overlay code: `UpsideDownPipe_High/Low` `$C470/$C475`, `MoveUpsideDownPiranhaP` `$C4C0`, `BlowPlayerAround` `$C4FE`, `SimulateWind` `$C550`, `DrawLeaf` `$C561`, `WindOn/WindOff` `$C5BE/$C5C2` (DATA2 and DATA4 carry identical copies; in the worlds 1-4 state `$C470+` holds menu code instead). DATA3 (`$C5D0+`) holds victory/final-room code (`ScreenSubsForFinalRoom` `$C5FE`, `PrintVictoryMsgsForWorld8` `$C642`, ...).
- Overlay-cache hazard: `smb_ws_world`/`smb_ws_actors` key their caches on `AreaData` address + area type + world. In SMB2J the same address holds different streams per disk state, and worlds A-D reuse `WorldNumber` 0-3. Key must add `FileListNumber` ($07F7) and `HardWorldFlag` ($07FB) (and `LevelNumber`).

## 3. Hook PCs and routines named by the widescreen sources

SMB1 sources: `game.toml` `[[mod_function_hook]]` (10 hooks), `[[inline_dispatch]]` `$8E04`, direct calls `func_XXXX`. All are in SM2MAIN, always resident.

| SMB1 name | SMB1 | SMB2J | Role in SMB1 mod | Instr (1/2J/match) | Status |
|---|---|---|---|---|---|
| RenderAreaGraphics | $88AE | $678B (`RenderAreaGraphics`) | mod_function_hook + "column" (game_widescreen.c) | 89/89/89 | identical |
| JumpEngine | $8E04 | $6C7D (`JumpEngine`) | [[inline_dispatch]] (JSR JumpEngine; inline word table) | 13/13/13 | identical |
| InitializeArea | $8FE4 | $6E39 (`InitializeArea`) | hook "area" | 53/54/50 | modified |
| AreaParserCore | $93FC | $720D (`AreaParserCore`) | hook "collision-column"; also called directly func_93FC_b0 (smb_ws_world.c decode) | 134/134/132 | modified |
| CheckpointEnemyID | $C26C | $8E50 (`CheckpointEnemyID`) | hook "enemy-init"; also called directly func_C26C | 11/11/11 | identical |
| InitPiranhaPlant | $C787 | $9398 (`InitPiranhaPlant`) | hook "plant-init"; also called directly func_C787 | 12/28/12 | modified |
| EnemyMovementSubs | $C905 | $953A (`EnemyMovementSubs`) | called directly func_C905 | 2/2/2 | identical |
| LargePlatformSubroutines | $C982 | $95B7 (`LargePlatformSubroutines`) | called directly func_C982 | 4/4/4 | identical |
| MoveLiftPlatforms | $D65B | $A295 (`MoveLiftPlatforms`) | called directly func_D65B | 11/11/11 | identical |
| OffscreenBoundsCheck | $D67A | $A2B4 (`OffscreenBoundsCheck`) | hook "enemy-cull" | 44/48/40 | modified |
| EnemiesCollision | $DA33 | $A69C (`EnemiesCollision`) | called directly func_DA33 | 64/68/55 | modified |
| EnemyToBGCollisionDet | $DFC1 | $AC4A (`EnemyToBGCollisionDet`) | called directly func_DFC1 | 190/197/176 | modified |
| GetEnemyBoundBox | $E243 | $AEE1 (`GetEnemyBoundBox`) | called directly func_E243 | 4/4/4 | identical |
| FlagpoleGfxHandler | $E54B | $B1F1 (`FlagpoleGfxHandler`) | hook "flag-gfx"; also called directly func_E54B | 53/53/53 | identical |
| DrawLargePlatform | $E5C8 | $B26E (`DrawLargePlatform`) | hook "large-platform-gfx"; also called directly func_E5C8 | 72/72/72 | identical |
| EnemyGfxHandler | $E87D | $B52C (`EnemyGfxHandler`) | hook "enemy-gfx"; also called directly func_E87D | 385/405/375 | modified |
| DrawSmallPlatform | $ED66 | $BA41 (`DrawSmallPlatform`) | hook "small-platform-gfx"; also called directly func_ED66 | 59/59/59 | identical |
| RelativeEnemyPosition | $F152 | $BE37 (`RelativeEnemyPosition`) | called directly func_F152 | 3/3/3 | identical |
| GetEnemyOffscreenBits | $F1AF | $BE94 (`GetEnemyOffscreenBits`) | called directly func_F1AF | 3/3/3 | identical |

Modified routines, what changed (from the aligned instruction diff plus the source):

- **InitializeArea** ($8FE4 -> $6E39): D06: secondary-hard-mode trigger changed: SMB1 = PrimaryHardMode ($76A) or world >= 5-3 (cmp World5/Level3); SMB2J = HardWorldFlag ($7FB, worlds A-D) or world >= 4-4 (cmp World4/Level4); extra JSR LoadPhysicsData ($C241) before OperMode_Task++. In SMB2J it is task 1 of GameModeSubs (task 0 = GameModeDiskRoutines).
- **AreaParserCore** ($93FC -> $720D): Same structure. Two immediate metatile IDs differ ($62->$63 water/world-8 cover, $54->$6B terrain replacement): SMB2J metatile numbering is shifted. Object dispatch table is different (see area_object_dispatch).
- **InitPiranhaPlant** ($C787 -> $9398): D04/D05: entry now begins with a world-dependent prologue that SELF-MODIFIES PRG-RAM (EnemyAttributeData+$0D and ChkPlayerNearPipe+3: green plants in worlds 1-3, red otherwise, red in A-D). Shared by ids $0D and $04 (upside-down). Reached by JSR (VerticalPipe path in SMB1) but by JMP from SetupPiranhaPlant in SMB2J.
- **OffscreenBoundsCheck** ($D67A -> $A2B4): D10: id $04 (UpsideDownPiranhaP) gets the same narrowed bounds and erase-exemption as PiranhaPlant/HammerBro; JumpspringObject erase-exemption REMOVED. cull_hook replaces this routine at hook level so its predicate must be re-derived from this body.
- **EnemiesCollision** ($DA33 -> $A69C): Id $04 exempt from enemy-enemy collision like PiranhaPlant ($0D).
- **EnemyToBGCollisionDet** ($DFC1 -> $AC4A): D11: id $04 exempt (returns early); metatile-$23 "enemy erases block-buffer cell" branch removed (compare is now #$20 and the ($06),y clear is gone) so enemies no longer write the collision buffer; ChkToStunEnemies reworked (PowerUp/Goomba bounce, no Enemy_State high-nybble preserve).
- **EnemyGfxHandler** ($E87D -> $B52C): D12: prologue writes 3 attribute bytes at EnemyAttributeData+$18..$1A (world-dependent, self-modifying PRG-RAM) on EVERY call; CheckForJumpspring case removed (jumpspring drawn elsewhere); Bowser/frame-count $EC/$03 setup reordered; id $04 treated like $0D in the defeated/animation checks.

Instruction-identical (hook bodies need no semantic port): RenderAreaGraphics ($88AE -> $678B), JumpEngine ($8E04 -> $6C7D), CheckpointEnemyID ($C26C -> $8E50), EnemyMovementSubs ($C905 -> $953A), LargePlatformSubroutines ($C982 -> $95B7), MoveLiftPlatforms ($D65B -> $A295), GetEnemyBoundBox ($E243 -> $AEE1), FlagpoleGfxHandler ($E54B -> $B1F1), DrawLargePlatform ($E5C8 -> $B26E), DrawSmallPlatform ($ED66 -> $BA41), RelativeEnemyPosition ($F152 -> $BE37), GetEnemyOffscreenBits ($F1AF -> $BE94).

Other routines the actor/world code depends on (closure roots; all resident, all matched; full list in TOML `tier = "callee"`): 
| SMB1 | SMB1 addr | SMB2J addr | Instr | Status |
|---|---|---|---|---|
| ProcessEnemyData | $C144 | $8D21 | 137/140/135 | modified |
| CheckRightBounds | $C164 | $8D41 | 121/124/119 | modified |
| HandleGroupEnemies | $C71B | $932C | 55/55/55 | identical |
| InitEnemyObject | $C226 | $8E03 | 4/4/4 | identical |
| ProcessAreaData | $9508 | $7319 | 58/58/58 | identical |
| GetBlockBufferAddr | $9BE1 | $7A22 | 14/14/14 | identical |
| RunEnemyObjectsCore | $C882 | $94B7 | 8/8/8 | identical |
| EraseEnemyObject | $C998 | $95CD | 10/10/10 | identical |
| GetScreenPosition | $B038 | $7B90 | 8/8/8 | identical |
| FindEmptyEnemySlot | $994A | $7791 | 8/8/8 | identical |
| GetAreaDataAddrs | $9C22 | $C2C3 | 78/80/74 | modified (overlay region) |
| LoadAreaPointer | $9C03 | $C2A4 | 9/9/9 | identical |
| MovePiranhaPlant | $D3B0 | $9FDC | 47/50/44 | modified |
| ChkPlayerNearPipe | $D3CF | $9FFB | 32/35/31 | modified |
| GameCoreRoutine | $AEEA | $7A47 | 71/71/67 | modified |
| GameRoutines | $B04A | $7BA2 | 2/2/2 | identical |
| EnemiesAndLoopsCore | $C047 | $8C23 | 19/19/19 | identical |

## 4. RAM variables

All widescreen-referenced RAM is at the **same address** in SMB2J. Status `resolved` = matched-instruction operand pairs (count shown); `name-only` = identical label+value in both disassemblies, no matched instruction touches it directly.

| Addr | SMB1 name | Pairs | Status | Use in SMB1 mod |
|---|---|---|---|---|
| $0008 | ObjectOffset | 154 | resolved | begin(): g_ram[8]=slot; collide_residents |
| $0009 | FrameCounter | 47 | resolved | collide_residents: FrameCounter&1 gate (g_ram[9]); update: g_ram[9]=0 when inactive |
| $000F | Enemy_Flag | 47 | resolved | array x6; field F_FLAG; plant scan; slot-5 flagpole flag = $14 |
| $0014 | Enemy_Flag+5 | 3 | resolved | flagpole flag slot (flag_hook, decode flag_x) |
| $0016 | Enemy_ID | 111 | resolved | array x6; field F_ID |
| $001B | Enemy_ID+5 | 3 | resolved | flagpole flag id slot ($30 = FlagpoleFlagObject) |
| $001E | Enemy_State | 146 | resolved | array; field F_STATE |
| $003C | HammerBroJumpTimer | 4 | resolved | per-slot field alias (fields[] index 19) |
| $0046 | Enemy_MovingDir | 68 | resolved | field F_DIR |
| $0058 | Enemy_X_Speed | 103 | resolved | field F_XS (aliases PiranhaPlant_Y_Speed, LakituMoveSpeed, ...) |
| $006D | Player_PageLoc | 43 | resolved | promote(): distance to Mario |
| $006E | Enemy_PageLoc | 50 | resolved | field F_PAGE; live_x |
| $0073 | Enemy_PageLoc+5 | 2 | resolved | flag slot page (decode flag_x) |
| $0086 | Player_X_Position | 51 | resolved | promote(): distance to Mario |
| $0087 | Enemy_X_Position | 60 | resolved | field F_X; live_x |
| $008C | Enemy_X_Position+5 | 2 | resolved | flag slot X (decode flag_x) |
| $00A0 | Enemy_Y_Speed | 66 | resolved | field F_YS |
| $00B6 | Enemy_Y_HighPos | 31 | resolved | field F_YH |
| $00CF | Enemy_Y_Position | 134 | resolved | field F_Y (+$d0 = slot 1); decode plant y |
| $00E7 | AreaData | 36 | resolved | AreaDataLow; area_data() |
| $00E8 | AreaDataHigh | 2 | resolved | area_data() |
| $00E9 | EnemyData | 39 | resolved | EnemyDataLow; ensure() |
| $00EA | EnemyDataHigh | 0 | name-only | ensure() |
| $010D | FlagpoleFNum_Y_Pos | 4 | resolved | flag_graphics: g_ram[0x10d]=0xb0 |
| $0110 | FloateyNum_Control | 7 | resolved | field |
| $0117 | FloateyNum_X_Pos | 3 | resolved | field |
| $011E | FloateyNum_Y_Pos | 5 | resolved | field |
| $0125 | ShellChainCounter | 5 | resolved | field |
| $012C | FloateyNum_Timer | 3 | resolved | field |
| $0200 | Sprite_Data | 28 | resolved | OAM shadow; begin() clears Y bytes |
| $0240 | Sprite_Data+$40 | 0 | name-only | capture_packet OAM read base (Enemy_SprDataOffset[0]=$40) |
| $036A | BowserGfxFlag | 17 | resolved | begin(): =0 |
| $03A2 | PlatformCollisionFlag | 21 | resolved | field; platform update |
| $03C5 | Enemy_SprAttrib | 7 | resolved | field F_ATTR |
| $03D8 | EnemyOffscrBitsMasked | 9 | resolved | field F_OFF |
| $0401 | Enemy_X_MoveForce | 12 | resolved | field F_XF |
| $0417 | Enemy_YMF_Dummy | 18 | resolved | field F_YD (PiranhaPlantUpYPos alias) |
| $0434 | Enemy_Y_MoveForce | 34 | resolved | field F_YF (PiranhaPlantDownYPos alias) |
| $0491 | Enemy_CollisionBits | 15 | resolved | field F_COLL; promote/collide |
| $049A | Enemy_BoundBoxCtrl | 20 | resolved | field F_BBOX |
| $0500 | Block_Buffer_1 | 0 | name-only | collision cache; begin() rebuilds |
| $05D0 | Block_Buffer_2 | 0 | name-only | 0x500+0xd0 |
| $06A0 | BlockBufferColumnPos | 6 | resolved | decode; begin_column hook |
| $06A1 | MetatileBuffer | 34 | resolved | decode/observe: 13 rows |
| $06CC | SecondaryHardMode | 24 | resolved | ensure(): enemy record bit $40 gate |
| $06E5 | Enemy_SprDataOffset | 23 | resolved | begin() =$40; gfx_hook oam_base |
| $06EA | Enemy_SprDataOffset+5 | 2 | resolved | flag_hook oam_base |
| $070F | FlagpoleCollisionYPos | 2 | resolved | flag_graphics |
| $071A | ScreenLeft_PageLoc | 20 | resolved | camera() |
| $071B | ScreenRight_PageLoc | 17 | resolved | begin(): local projection; group member calc |
| $071C | ScreenLeft_X_Pos | 16 | resolved | camera() |
| $071D | ScreenRight_X_Pos | 12 | resolved | begin(); spawn_hook member calc |
| $071F | AreaParserTaskNum | 9 | resolved | observe_column side |
| $0720 | CurrentNTAddr_High | 7 | resolved | observe_column nt |
| $0721 | CurrentNTAddr_Low | 9 | resolved | observe_column tx |
| $0723 | ScrollLock | 11 | resolved | decode: fixed-room detection |
| $0725 | CurrentPageLoc | 19 | resolved | decode/observe: col = page*16+colpos |
| $0726 | CurrentColumnPos | 11 | resolved | decode/observe |
| $0727 | TerrainControl | 4 | resolved | decode header1&15 |
| $072C | AreaDataOffset | 15 | resolved | decode: $FD terminator check |
| $0730 | AreaObjectLength | 28 | resolved | decode init =$FF (x3: $730-$732) |
| $0731 | AreaObjectLength+1 | 1 | resolved | decode |
| $0732 | AreaObjectLength+2 | 1 | resolved | decode |
| $0733 | AreaStyle | 2 | resolved | decode header1>>6 |
| $0739 | EnemyDataOffset | 27 | resolved | spawn_hook |
| $0741 | ForegroundScenery | 3 | resolved | decode header0&7 (<4) |
| $0742 | BackgroundScenery | 3 | resolved | decode header1>>4&3 |
| $0743 | CloudTypeOverride | 10 | resolved | decode style==3 |
| $0744 | BackgroundColorCtrl | 3 | resolved | decode header0&7 (>=4) |
| $0747 | TimerControl | 38 | resolved | update gates |
| $074C | InitializeMemory clear bound | 0 | name-only | decode memset(g_ram,0,0x74c) mirrors InitializeArea ldy #$4b |
| $074E | AreaType | 44 | resolved | gameplay cache key; collide_residents |
| $075F | WorldNumber | 20 | resolved | cache key |
| $076A | PrimaryHardMode | 11 | resolved | ensure(): Goomba->BuzzyBeetle mutate |
| $0770 | OperMode | 6 | resolved | gameplay() |
| $0772 | OperMode_Task | 14 | resolved | gameplay() |
| $0776 | GamePauseStatus | 0 | name-only | update run gate |
| $077F | IntervalTimerControl | 1 | resolved | timer decrement gate |
| $078A | EnemyFrameTimer | 17 | resolved | field F_TIMER |
| $0796 | EnemyIntervalTimer | 21 | resolved | field F_INTERVAL |

Notes: `$14/$1B/$73/$8C/$6EA` are slot-5 (flagpole flag object) members of the per-slot arrays; `$240` is OAM byte `$200+$40` (Enemy_SprDataOffset[0]=$40); `$5D0` = `Block_Buffer_2`; `$74C` is the InitializeMemory clear bound (SMB2J `ldy #$4B` identical). `GameTimerDisplay` moved ($7F8 -> $7EC) but the widescreen sources do not use it.

SMB2J-only RAM that matters: `IRQUpdateFlag` $0722 (inside the area-parser variable block), `NameTableSelect` $077A, `IRQAckFlag` $077B, `FileListNumber` $07F7, `WindFlag` $07F9, `HardWorldFlag` $07FB, `DiskIOTask` $07FC, `AreaAddrsLOffset` $074F. SMB1-only and unused by the mod: `Sprite0HitDetectFlag`, `CurrentPlayer`, `OffScr_*`.

## 5. Tables and formats

| Item | SMB1 | SMB2J | File/residency | Method |
|---|---|---|---|---|
| MetatileGraphics_Low | $8B08 | $69E5 | SM2MAIN, always resident | label + RenderAreaGraphics instruction-identical (indexes it with the same LDA abs,y pair) |
| MetatileGraphics_High | $8B0C | $69E9 | SM2MAIN, always resident | as above |
| Palette0_MTiles | $8B10 | $69ED | SM2MAIN, always resident | label in both disassemblies |
| Palette1_MTiles | $8BAC | $6A7D | SM2MAIN, always resident | label in both disassemblies |
| Palette2_MTiles | $8C64 | $6B3D | SM2MAIN, always resident | label in both disassemblies |
| Palette3_MTiles | $8C8C | $6B71 | SM2MAIN, always resident | label in both disassemblies |
| BlockBufferAddr / Block_Buffer_1,_2 | 0x0500 / 0x05D0 | 0x0500 / 0x05D0 | RAM, n/a | GetBlockBufferAddr instruction-identical 14/14; equ values identical |
| EnemyAttributeData | $E85B | $B50A | SM2MAIN, always resident (PRG-RAM, WRITABLE) | label |

- Metatile decode used by the world cache (`tile = table[(meta&63)*4 + x*2 + y]`, palette = `meta>>6`, 13 rows, `MetatileBuffer` $06A1, block buffers $0500/$05D0) is structurally unchanged; `RenderAreaGraphics` and `GetBlockBufferAddr` are instruction-identical. **Metatile IDs are renumbered** (per-bank counts SMB1 39/46/10/6 vs SMB2J 36/48/13/7, estimated from label spacing), so nothing keyed on SMB1 IDs may be reused.
- Area header (2 bytes before the object stream: foreground/bg-colour bits, entrance, timer; terrain, background scenery, style with 3 = cloud override) is decoded identically; `AreaData` is advanced by 2 after the header, so `rom(data-2..-1)` remains valid. Object terminator `$FD`; enemy stream terminator `$FF`; enemy record layout, row 14 (3 bytes), row 15 (page), `$40` hard-mode and `$80` page bits, group ids `$37-$3E` (bodies at trigger-48, 24 px spacing), Goomba->Buzzy on `PrimaryHardMode`: all instruction-identical (`ProcessEnemyData` 135/137, `CheckRightBounds` 119/121, `HandleGroupEnemies` 55/55).

### Constants

| Item | SMB1 | SMB2J | Evidence |
|---|---|---|---|
| PiranhaPlant enemy id | 0x0D | 0x0D | CMP #$0D sites matched in EnemiesCollision/OffscreenBoundsCheck; equ identical |
| UpsideDownPiranhaP enemy id | none (id $04 = NoInitCode) | 0x04 | SMB2J-only: init table id 4 = InitPiranhaPlant, move table id 4 = MoveUpsideDownPiranhaP; area objects UpsideDownPipe_High/_Low (row-15 large ids 6/7 in SM2DATA2/4) |
| FlagpoleFlagObject enemy id | 0x30 | 0x30 | FlagpoleObject sets Enemy_ID+5 := $30 (both); RunEnemyObjectsCore table position identical |
| managed ids <= $10 except 4 and 9 | 0x00-0x10 | 0x00-0x10 (id 4 differs: UpsideDownPiranhaP) | init/move/run tables compared entry by entry (only id 4 differs) |
| balance platform / lifts | 0x24 balance, 0x25-0x2C | 0x24, 0x25-0x2C | RunEnemyObjectsCore ids $24-$2A = RunLargePlatform, $2B-$2C = RunSmallPlatform in both; init table identical from id $24 |
| group enemy ids | 0x37-0x3E | 0x37-0x3E | CheckForEnemyGroup cmp #$37/#$3F identical; HandleGroupEnemies instruction-identical |
| area terminator | 0xFD | 0xFD | DecodeAreaData cmp #$FD matched |
| enemy terminator / page rows | $FF end; row 14 = 3 bytes; row 15 = page ctrl; b1&$80 page flag; b1&$40 hard mode | identical | ProcessEnemyData/CheckRightBounds matched (135/137) |
| OperMode values | 1 = game, 2 = victory, 0 = attract | 1, 2, 0 (same) | OperModeExecutionTree order identical (AttractModeSubs, GameModeSubs, VictoryModeMain, GameOverSubs) |
| OperMode_Task in-game | 3 (GameCoreRoutine is task 3) | 4 (GameCoreRoutine is task 4; task 0 = GameModeDiskRoutines) | GameCoreRoutine cmp #$03 -> cmp #$04; GameModeSubs has 5 entries |
| area data address validity | $8002..$FF00 | $6002..$DFFF (streams live in $C260-$D29E) | labels L_*/E_* |
| OAM offset of resident enemy slot 0 | $40 | $40 (Enemy_SprDataOffset default identical) | EnemyGfxHandler matched |
| metatile ids | SMB1 numbering | RENUMBERED (e.g. flag top $24->$21) | FlagpoleObject/AreaParserCore immediates |

### Enemy dispatch tables (id -> handler)

- enemy_init (CheckpointEnemyID): SMB1 $C282, SMB2J $8E66, 54 entries; differences: idx 4: SMB1 NoInitCode / SMB2J InitPiranhaPlant
- enemy_move (EnemyMovementSubs): SMB1 $C90A, SMB2J $953F, 21 entries; differences: idx 4: SMB1 MoveNormalEnemy / SMB2J MoveUpsideDownPiranhaP
- enemy_run (RunEnemyObjectsCore, id-$14): SMB1 $C892, SMB2J $94C7, 34 entries; differences: none
- Platforms: ids `$24`-`$2A` RunLargePlatform, `$2B`-`$2C` RunSmallPlatform, init `$24` InitBalPlatform ... in both games (identical entry by entry), so `platform()`, `managed()`, kinds `$26/$27/$2B+` shifted-by-12 logic port unchanged. Only id `$04` differs.

### Area object dispatch (DecodeAreaData)

SMB1 $9667 (47 entries) / SMB2J $7478 (55 entries). Index = handler slot; offsets differ: small objects `$16` -> `$18`, row-13 page-control `$22` -> `$28`, row 14 fixed `$2E` -> `$36`. SMB2J inserts UpsideDownPipe_High/_Low at 22-23, extra QuestionBlock variants and PoisonMushBlock among small objects, WindOn/WindOff at row-13 n=12/13 (indexes 52/53); SMB1 MushroomLedge (row-13 n=14) is SMB2J CloudLedge (different body)

Because the widescreen world cache runs the game's own `AreaParserCore`, object IDs matter only for: (a) which overlay must be loaded for the decode to be valid (WindOn/WindOff/UpsideDownPipe live in DATA2/DATA4 only), (b) plant capture, (c) fixed-room (`ScrollLock` $723) detection.

## 6. Self-modifying PRG-RAM writes in the closure

| Routine | Instruction | Target |
|---|---|---|
| InitPiranhaPlant | $93B4 | $B517 |
| InitPiranhaPlant | $93B9 | $9FFE |
| EnemyGfxHandler | $B53E | $B522 |
| EnemyGfxHandler | $B541 | $B523 |
| EnemyGfxHandler | $B544 | $B524 |

`InitPiranhaPlant` patches `EnemyAttributeData+$0D` ($B517) and `ChkPlayerNearPipe+3` ($9FFE); `EnemyGfxHandler` patches `EnemyAttributeData+$18..$1A` ($B522-$B524) on every call. All are world-dependent and idempotent, but the host private-context runs (`begin()/end()` restore only $0000-$07FF and CPU state) do not roll PRG-RAM back. Also: SMB2J hooks at `InitPiranhaPlant` are entered by `JMP` from `SetupPiranhaPlant`, not `JSR`.

## 7. Semantic differences likely to matter (severity H/M/L)

- **D01 H: gameplay predicate.** SMB1 `OperMode==1 && OperMode_Task==3`. SMB2J GameModeSubs has 5 tasks (0 GameModeDiskRoutines, 1 InitializeArea, 2 ScreenRoutines, 3 SecondaryGameSetup, 4 GameCoreRoutine) and `GameCoreRoutine` compares `#$04`: use `OperMode_Task==4` (static evidence; not yet observed live).
- **D02 H: overlay-resident data.** Area/enemy streams, area pointer tables, wind and upside-down-pipe code are in the $C2B4-$D29E overlay region; valid `AreaData` range is $6002..$DFFF; reads must use the RAM view; caches must be keyed/invalidated by `FileListNumber` and `HardWorldFlag` (disk reload of a different file at the same address).
- **D03 H: worlds.** Worlds 1-8 as SMB1, world 9 = `WorldNumber` 8 (loops forever; per the disassembly README its data/code is with DATA3/the ending state, which of its areas live in which file was not mapped), worlds A-D = `HardWorldFlag` set with `WorldNumber` 0-3 (DATA4). Area tables `World1Areas..World9Areas`; `WorldNumber`-only cache keys collide between 1-x and A-x.
- **D04 H: upside-down piranha plants (id $04).** SMB2J-only enemy id $04: created by area objects `UpsideDownPipe_High/_Low` (row-15 large ids 6/7, DATA2/DATA4 only) via `SetupPiranhaPlant(#4)`; init = shared `InitPiranhaPlant`, move = `MoveUpsideDownPiranhaP` ($C4C0, overlay); Y up/down reversed, `PiranhaPlant_MoveFlag` preset 1; excluded from cull/collision like id $0D. SMB1 mod only captures id `$0D` in the decode and `managed()` explicitly excludes 4. Also plants now spawn in every vertical pipe (SMB1 skipped 1-1) and `VerticalPipe` calls `SetupPiranhaPlant`.
- **D05 M: self-modifying attribute data.** See section 6; private runs of `InitPiranhaPlant`/`EnemyGfxHandler` write PRG-RAM outside the saved $0-$7FF window (and thus also outside the host savestate window if it snapshots RAM only).
- **D06 M: InitializeArea.** Secondary hard mode: SMB1 = PrimaryHardMode or world >= 5-3; SMB2J = HardWorldFlag or world >= 4-4. Adds `JSR LoadPhysicsData` ($C241). The host decode mimics InitializeArea by hand (memset $74C, header decode): header logic verified identical, but it reads `SecondaryHardMode`/`PrimaryHardMode` live so nothing else changes.
- **D07 L: InitializeMemory.** Preserves $0100-$0108 (FDS BIOS variables). Host decode unaffected.
- **D08 H: status-bar split.** SMB2J has no sprite-0 hit: NMI arms the FDS IRQ timer ($1658 cycles) when `IRQUpdateFlag` ($0722) is set; `IRQHandler` writes PPUCTRL (`NameTableSelect` $077A) and `HorizontalScroll`/`VerticalScroll` for the playfield. Renderer must take the top-32-line HUD/playfield split from the IRQ path, not sprite-0 (status-bar height for SMB2J not measured).
- **D09 M: area object set.** SMB2J adds WindOn/WindOff (page-control ids, wind leaves drawn by DATA2/DATA4 `DrawLeaf` into OAM at native positions only), UpsideDownPipe_High/Low, extra QuestionBlock variants and `PoisonMushBlock` (poison mushrooms, PowerUpObject $2E), replaces MushroomLedge by CloudLedge. Metatile IDs renumbered.
- **D10 M: OffscreenBoundsCheck (cull_hook $D67A).** Id $04 treated as PiranhaPlant/HammerBro (narrow bounds, never erased offscreen); JumpspringObject erase exemption removed (jumpspring is now erased when offscreen). Port the predicate from the SMB2J body.
- **D11 M: enemy/BG collision.** Enemies no longer erase metatile-$23 cells in the block buffer (SMB1 `sta ($06),y`); id $04/$0D not demoted; ChkToStunEnemies rewritten. Fewer collision-cache side effects for resident actors.
- **D12 M: EnemyGfxHandler.** World-dependent attribute patching on every call, jumpspring case removed, id $04 handled with $0D. Sprite count per enemy and OAM base ($6E5) unchanged.
- **D13 M: flagpole / flag.** Flag object is still slot 5, id $30, Y=$30, X=column*16-8; `FlagpoleGfxHandler` instruction-identical; `FlagpoleObject` builds with renumbered metatile IDs ($21/$22/$62). Victory-mode flagpole flow otherwise SMB1-like.
- **D14 M: victory mode ($770==2).** SMB1 mod treats every OperMode 2 frame as gameplay. In SMB2J worlds 8/D run `VictoryModeSubsForW8` (disk-load tasks 5 and $0D blank screen, final room, mushroom retainers, ending) using DATA3 overlay code; those screens are not area-parser scenes. Restrict widescreen to the castle-walk tasks or verify each task.
- **D15 M: fixed / bonus rooms.** Scroll lock toggled by `ScrollLockObject` ($723), unchanged; warp-zone objects rewritten (`ScrollLockObject_Warp` codes $80-$8B, WriteWarpZoneMessage, kills only id $0D). IntroPipe and the other page-control handlers keep their bodies (indexes shifted), so first-24-column lock detection should still identify pipe intros and bonus rooms, but this was not tested; re-check SMB2J warp-zone rooms and world 9.
- **D16 M: wind.** `WindFlag` $07F9; per-frame `SimulateWind` (called from the game engine path of GameCoreRoutine) and `BlowPlayerAround` (OnGroundStateSub) run whenever `FileListNumber`!=0, i.e. every state except worlds 1-4. Leaves (DrawLeaf) are OAM sprites in the overlay code, not enemies, and stay 4:3-only.
- **D17 L: enemy activation window.** Identical: +$30 px right-bound test, group/frenzy handling. Lakitu/Spiny/Bowser/Hammer Bro init routines differ slightly (extra world logic), all outside `managed()`.
- **D18 L: player-count logic.** `GameCoreRoutine` has no two-player prologue (`CurrentPlayer`, `SavedJoypadBits` swap).

## 8. Unresolved / not verified

- **MushroomLedge (SMB1 $9778)**: no SMB2J label of that name; nearest CloudLedge ($75CF) matches only 15/29 instructions: treated as a different object. Reached only through the area parser dispatch (real code executes), never named by the widescreen sources.
- **PlayerEndWorld (SMB1 $8461)**: replaced by EndCastleAward/EndWorld1Thru7 and a world-8 sub-list; not named by widescreen sources.
- **OperMode_Task==4 in live play**: established statically (GameCoreRoutine compare + GameModeSubs table); not observed in a running build in this pass.
- **HUD split scanline**: SMB2J splits with an FDS IRQ timer ($1658 cycles armed in NMI, IRQHandler rewrites PPUCTRL/scroll), not sprite-0. The status-bar height (SMB1: 32 px) was not measured for SMB2J.
- **overlay state combinations**: images were built as main+one overlay; a real session can stack e.g. data4 then data2 (data4 remnants in $C2B4-$C46F). Only routines in $C2B4+ are affected; none of the 19 hook/direct PCs are.
- Data tables referenced by matched routines (enemy graphics offset tables, sprite tile tables, metatile tables) were not byte-compared beyond their addresses; their layouts follow from instruction identity, and contents differ where SMB2J renumbered metatiles.
- No dynamic (running build) confirmation was done in this pass; every claim is static.

## 9. Reproduction

Reference build: `git clone threecreepio/smb2j-disassembly`; `ca65 -g --debug-info fdswrap.asm`; `ld65 --dbgfile main.fds.dbg -C layout fdswrap.o -o main.fds` (cc65 in `nesrecomp/_refs/cc65`), compare with the image body (skip 16-byte fwNES header). Scripts were kept in the session scratchpad (not committed).
