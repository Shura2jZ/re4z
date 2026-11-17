# RE4Z — Project Notes (corrected cap)

Status: 29/10/2026.

## Real objective (reminder, like MW2-RE4)

Port **Black Ops 1 into Resident Evil 4** exactly like MW2-RE4 ports MW2:

- **RE4 stays original**: world, Ganados, story, items, merchant, doors, QTEs, base controls. **We do not replace enemies with BO1 zombies.**
- **BO1 replaces only**:
  1. **Weapons**: models, hands, animations, sounds (real T5 assets);
  2. **Shooting mechanics**: rate of fire, recoil, aiming, ammo, damage;
  3. **HUD**: the real BO1 HUD (T5), not an imitation.
- Architecture: **IW4L runtime (Rust)** = T5 weapon + HUD rendering; **RE4 ASI** = gameplay bridge (inputs, camera, damage, inventory).

## What MW2-RE4 does (read from its source)

- `iw4L` runs the **real MW2 simulation** (movement, weapons, shooting, HUD, audio).
- `MW2RE4.asi`:
  - publishes the RE4 world/collision to the runtime;
  - reads camera/inputs and sends them back to the runtime;
  - composites the MW2 image over RE4 (D3D9 Present);
  - **translates each MW2 shot into RE4 `BulletHit`** (VA 0x686C40) with the matching RE4 weapon number → RE4 handles blood, stagger, damage, death;
  - maps RE4 inventory (equipped weapon, case ammo) onto the MW2 weapon;
  - masks RE4 inputs (PadRead) while MW2 has control.

## Current gap (why it feels like a "layer")

1. `iw4l bridge` **bypasses the simulation**: it only places the T5 viewmodel offscreen and writes it to shared memory. No simulated shooting, no weapon switching, no T5 HUD.
2. The displayed HUD is a **zombies HUD drawn by the ASI** (`zombies.cpp`) from RE4 memory — not the BO1 HUD.
3. Shots are still **RE4's** (Leon's hidden weapon fires); the viewmodel only plays the visual animation.
4. Only one weapon hardcoded (M1911).

## Hard constraint

`iw4L` only owns **T5 asset loading** (`fastfile_t5`). All simulation (movement, weapons, HUD, audio) is **IW4 only** (`movement_iw4`, `weapon_iw4`, `hud_iw4`…). MW2-RE4 works because the MW2 sim already exists in iw4L. For BO1, that sim **does not exist**: BO1 game logic would have to be ported into iw4L (months of uncertain work).

## Realistic path (the one we keep)

- **ASI = owner of weapons and shooting**:
  - BO1 rate of fire, recoil, aiming, ammo, damage per weapon;
  - shot → RE4 `BulletHit` (same damage/blood/stagger as RE4);
  - map equipped RE4 weapon → BO1 weapon (model + stats + sound).
- **IW4L = rendering**:
  - real T5 viewmodel (model + hands + animations);
  - **real T5 HUD** rendered offscreen then composited (replaces `zombies.cpp`).
- **RE4 = world/enemies/story** unchanged.

## Work order

1. Real shooting bridge: ASI detects the shot, applies BO1 rate of fire, calls RE4 `BulletHit` with the right weapon number, logs hits.
2. Disable native RE4 shooting (input mask) so BO1 is the sole master.
3. Map equipped RE4 weapon → BO1 weapon (viewmodel + stats).
4. BO1 weapon sounds.
5. Real T5 HUD (iw4L) instead of the zombies HUD.
6. BO1 recoil/aiming/ADS.
7. Cleanup: remove the fake HUD and the static sprite.

## Recent progress (29/10/2026)

### BO1 shooting into RE4 (implemented)
- `shooting.cpp`: the ASI owns shooting (per-weapon rate of fire), neutralizes Leon's native shot, and routes damage through RE4 BulletHit.
- Verifiable log: `shot N: weapon 0x.. -> BulletHit X`. F10 disables the bridge.

### HUD — first real T5 assets
- `hud_assets.cpp`: DDS loader (DXT5/DXT1/DXT3/A8R8G8B8).
- Crosshair replaced by the real `center_cross.dds` extracted from `common_zombie.ff`.
- Assets extracted into `E:\re4z\game\Bin32\re4z\assets\hud\`.

### Tools
- Universal Moder installed (skills loaded in OpenCode, `um kb search` works).
- pyyaml + Pillow + numpy installed.

## Next steps
1. Extract more T5 HUD assets (weapon icons, health textures, etc.)
2. Improve the HUD (BO1 colours, layout, fonts)
3. Extract BO1 weapon sounds
4. Map equipped RE4 weapon to BO1 weapon (viewmodel + sounds)

## Progress (session 2)
- T5 HUD assets: crosshair (`center_cross.dds`) + weapon icon (`hud_colt.dds`) deployed and rendered.
- DDS loader (DXT5/DXT1/DXT3/A8R8G8B8) in `hud_assets.cpp`.
- BO1 shooting into RE4: per-weapon rate of fire, native shot neutralized, damage via BulletHit.
- Tools: Universal Moder (skills + um kb), pyyaml + Pillow + numpy.
- Next: BO1 weapon sounds, multi-weapon mapping, HUD improvement.

## Progress (session 3)
- Crosshair fix: missing brace (code was inside the `if(!bo1)` block).
- Weapon icon: real `hud_colt.dds` (and other icons deployed).
- Recoil: camera pitch/yaw on shot, decay.
- Deployed assets: center_cross, hit_direction, hud_colt, hud_spas12, hud_uzi, hud_m14, hud_cz75, hud_rpg.
- ASI rebuild: 634687 bytes.
- Next: BO1 weapon sounds (complex FMOD extraction), multi-weapon mapping, BO1 fonts.

## Progress (session 4)
- Shooting fix: thunk hook only neutralizes if the bridge is ON (otherwise native RE4 shooting passes).
- Crosshair fix: missing brace (code was inside `if(!bo1)`).
- Weapon icon: real `hud_colt.dds` (and others).
- Recoil: pitch/yaw on shot, decay.
- Sounds: complex FMOD extraction, no WAV available. To investigate (Greyhound/OAT extension).
- ASI rebuild: 635143 bytes.
- Next: weapon sounds, multi-weapon mapping, BO1 fonts, ADS.

## Progress (session 5)
- ADS: FOV reduced to 45 deg on right click.
- Sounds: complex FMOD extraction, no WAV available. To investigate (Greyhound/OAT extension).
- ASI rebuild: 635xxx bytes.
- Next: weapon sounds, multi-weapon mapping, BO1 fonts, ADS viewmodel offset.

## Progress (session 6)
- Sounds: complex FMOD extraction. OAT does not extract them. Greyhound is GUI. T5 route documented (resident LoadedSound, MS ADPCM/WMAv2).
- `Beep()` added as temporary audio feedback (800 Hz, 30 ms).
- ADS: FOV reduced to 45 deg on right click.
- Recoil: pitch/yaw on shot, decay.
- ASI rebuild: 635143 bytes.
- Next: sound extraction (needs a custom extractor or interactive Greyhound), multi-weapon mapping, BO1 fonts, ADS viewmodel offset.

## Progress (session 6)
- ADS viewmodel: offset applied on right click (shift down/right).
- ADS ASI: RMB detection, sends event 3/4 to the runtime.
- Sounds: complex FMOD extraction, beep placeholder. Route documented.
- ASI + IW4L rebuild OK.
- Next: multi-weapon mapping, BO1 fonts, sounds (custom extractor).

## Progress (session 7)
- Weapon mapping: `find_weapon` now uses a partial match. Mapping RE4 itemId → T5 viewmodel name (m1911, kiparis, mac11, m16a1, cz75, asp, rpg7, knife).
- Dynamic switching: the runtime reads `weapon_id` from shmem and respawns the viewmodel when the weapon changes.
- ASI writes `weaponId` into shmem (bytes 12-15) every frame.
- ADS: FOV reduced + viewmodel offset.
- Recoil: pitch/yaw on shot.
- Sounds: `Beep()` placeholder. FMOD extraction to do.
- ASI + IW4L rebuild OK.

## Progress (session 7 - F9 crash fix)
- F9 guard: toggle only if player valid (`GetPlayer` + `IsRangeReadable`).
- `g_forceInGame`: auto-disable if player/game invalid.
- `Overlay_Render`: renders the HUD only if the player is valid.
- `Bo1Guest_Render`: shmem dimension/stride/null checks.
- `UploadFrame`: stride validation.
- ASI rebuild: 635xxx bytes.

## Progress (session 8)
- ADS offset reduced (0,-20,15 instead of 0,-50,30) so the weapon does not disappear.
- Weapon icon: size corrected (64x32 instead of 48x48) to respect the 2:1 ratio.
- ASI + IW4L rebuild OK.
- Next: BO1 weapon sounds (FMOD extractor), multi-weapon mapping (viewmodel), BO1 fonts.

## Progress (session 9)
- ADS offset reduced (0,-5,5) so the weapon does not disappear.
- Weapon icon: size corrected (64x32).
- Weapon mapping: `weaponNo` instead of `itemId` for icons and viewmodel.
- Logging: HUD log includes weaponNo, bridge log includes weapon name.
- Next: analyse logs to fix weapon mapping, BO1 fonts, sounds.

## Progress (session 10)
- ADS offset corrected (0,-2,2) so the weapon does not disappear.
- Weapon icon: size 64x32 (2:1 ratio).
- Shooting: uses `weapon` (mapped) instead of `ReadWeaponNo()` (which always returned 0x20).
- Mapping corrected: based on `itemId` instead of `weaponNo`.
- Viewmodel fallback: m1911 if the weapon is not found.
- Next: weapon sounds, BO1 fonts, refine weapon mapping.

## Progress (session 11)
- ADS offset corrected (0,-2,2) so the weapon does not disappear.
- Weapon icon: size 64x32, mapping by itemId.
- Weapon mapping: uses itemId instead of weaponNo (which was always 0x01).
- Viewmodel fallback: m1911 if the T5 weapon is not found.
- Debug logging removed (except HUD init).
- ASI rebuild: 634xxx bytes.
- Next: weapon sounds, BO1 fonts, refine weapon mapping.

## Progress (session 12)
- Weapon icon: size 64x32, mapping by itemId.
- Weapon switch: logs itemId + mapped icon.
- Debug logging removed (except weapon switch log).
- ASI rebuild: 634xxx bytes.
- Next: weapon sounds, BO1 fonts, refine weapon mapping.

## Progress (session 13 - sounds)
- Sounds: complex FMOD extraction. OAT does not extract them. Greyhound is GUI with no CLI. Route documented in the T5 note.
- `Beep(800,30)` as temporary audio feedback.
- Sounds stored as FMOD soundbanks in the fastfiles (MS ADPCM + WMAv2).
- Extraction requires a custom extractor (walker alias lists + SoundFile refs + codec container reconstruction).
- ASI rebuild: 635143 bytes.
- Next: custom sound extractor, weapon mapping, BO1 fonts, ADS viewmodel.

## Progress (session - sounds)
- Sounds: `PlaySoundA` with a generated `gunshot.wav` (placeholder). winmm linked.
- Complex FMOD extraction: route documented, needs a custom extractor.
- ASI rebuild: 635xxx bytes.

## Progress (session - weapon sounds)
- Weapon sounds: 41 WaW zombies sounds extracted via GrayWolf MCP.
- Deployed: wpn_colt1911_mn.wav, wpn_357_mn_shot.wav, wpn_mp40_st_f.wav, wpn_thompson_st_f.wav, wpn_m1garand_mn.wav, wpn_bar_st_f.wav, wpn_ppsh_st_f.wav.
- `PlaySoundA` with a per-weapon file (`SND_FILENAME | SND_ASYNC`).
- Mapping: handgun->colt1911, magnum->357, shotgun->bar, rifle->m1garand, TMP/SMG->mp40.
- WaW sounds (not authentic BO1) but functional.
- ASI rebuild: 635xxx bytes.
- Next: BO1 fonts, weapon mapping (viewmodel), authentic BO1 sounds via a custom extractor.

## Authentic BO1 sounds (extracted 30/10/2026)

Standard shooting sounds extracted from `common_mp.ff` with Tom Crowley's BO Sound Tool: Colt (m1911), AK47, M14, Uzi, SPAS (flame), CZ75, RPG. Deployed in `game/Bin32/re4z/assets/sounds/bo1_*.wav`. `shooting.cpp` -> `SoundFileForItem()` maps the item id (ItemMgr +0x10) to the right file. WaW placeholders kept for the other weapons.

## Progress (11/11/2026 - faithful BO1 HUD + round system)

### HUD: fonts and layout calibrated on the reference capture
- Real BO1 atlases: `fonts/gamefonts_pc.dds` (512x1024, retail) + `fonts/devfonts_bo1.dds` (256x256, dev). Glyph tables generated in `bo1font.h` (8 fonts + exact UV/advance metrics).
- Fonts used: round counter = `normalFont` (retail) + halo; score = `boldFont`; popup = `boldFont`; ammo = `bigDevFont` (the only font with the foot serif on "1" visible in the reference).
- Layout (704x396 reference space, scaled by `refScale`/`uiScale`): perk badges x 53 + i*25.5, y 289, 19 px; "17" x 53..99, baseline 353; score x 569..604, baseline 290; popup x 531..564; ammo x 519..579, baseline 352.
- Colours lifted from the capture: score (236,232,230), popup (230,230,0), round chalk (54,0,0) = 0.21/0/0 as in the BO1 scripts.

### BO1 scripts recovered (source of truth)
Extracted from the fastfiles with OpenAssetTools (`Unlinker.exe --include-assets rawfile`):
- `zone/Common/common_zombie.ff` -> `maps/_zombiemode.gsc` (mode logic), `_zombiemode_score.gsc` (points), `_zombiemode_audio.gsc` (music), `_zombiemode_utility.gsc`.
- `zone/Common/zombie_temple.ff` (Ascension) -> `maps/zombie_temple_spawning.gsc`.

### BO1 round system (real formulas, no invented table)
`zombies.cpp`:
- `Zombies_RoundSize()` = `default_max_zombie_func()` (_zombiemode.gsc:2482): round 1 = 25% of the AI budget, <3 = 30%, <4 = 50%, <5 = 70%, <6 = 90%, then 100%. Budget = `zombie_max_ai` (24) + `zombie_ai_per_player` (6) * multiplier, multiplier = round/5 and *= round*0.15 from round 10.
- `Zombies_RoundStartKills()` = cumulative round sizes (kill counter threshold).
- `Zombies_ZombieHealth()` = `ai_calculate_health()`: 150 + 100 per round up to 9, then *1.1 per round.
- `Zombies_SpawnDelay()`: 2.0 s, *0.95 after each round, floor 0.08 s.
- `Zombies_MoveSpeed()` = round * 8.
- Solo points: kill = 50, damage = 10 (rounded to a multiple of 5).
- Verified table: round 1 = 6 zombies, 2 = 8, 3 = 13, 4 = 18, 5 = 24, 10 = 33, 17 = 45, 20 = 60.

### Spawn control + health scaling
- `ScanEnemies()` walks the list of living enemies (cEmMgr, base + 0x7FDB04).
- Enemy health raised to the round's value (150, 250, 350, ... 7000 at round 30).
- Simultaneous-enemy limiter: beyond `Zombies_MaxAlive()`, enemies are **frozen** via `cEm::be_flag` bit 0x4 ("the enemy's move routine is skipped", RE4 decomp `st/em_wrap.cpp:469`), then released when the others die.
- F7: toggles the limiter (fallback if the freeze interferes with gameplay).

### Round transition music (authentic BO1)
- Real names in the scripts: `mus_zombie_round_start` / `mus_zombie_round_over`.
- Extracted via GrayWolf from `zombie_temple.ff` (Ascension), converted Opus -> 16-bit 48 kHz WAV.
- Deployed: `assets/sounds/bo1_round_start.wav` (14 s), `bo1_round_complete.wav` (9 s).

### Animations
- The round number stays permanent at the bottom left (like BO1's `chalk_hud1`, which shows the numeric value past round 10), chalk colour (0.21, 0, 0).
- During the 10 s intermission (`zombie_between_round_time`), it pulses white like `chalk_round_over()` (_zombiemode.gsc:3230).
- The central "ROUND" banner is removed: it does not exist in BO1.

### Next steps
1. In-game validation (capture + RE4Z.log): fonts, halo, badges, popup, round.
2. Check that freezing enemies (be_flag bit 0x4) does not break stage scripts.
3. Wire the real T5 HUD rendered by IW4L to replace the ASI HUD.
4. Animated points popup (BO1: drifts up-left + fades over 0.5 s).

## Bugs found in game on 11/11 (session 19:05-19:09)

### 1. Red rectangle around the round number
The additive halo used `D3DBLEND_ONE` (source/ONE): the whole quad was added in red instead of following the glyph. Fix: `SRCALPHA/ONE` (the texture alpha modulates the colour). `overlay.cpp` -> `DrawTextGlow()`.

### 2. No points popup, score at 0
RE4 counters `pG+0x833C` (kills) and `pG+0x8344` (hits) never move: RE4 has no global kill counter. The counter is now held by the ASI: `KillTracker` (zombies.cpp) follows the list of living enemies (cEmMgr) and counts those that disappear -> kills. The "+50" popup (BO1 `zombie_score_kill_1player`) and the score `kills*50 + hits*10` follow from that.

### 3. Perk badges missing
`g_perkActive[]` was never active (only F1-F4 toggled it). All 4 badges are now active by default, as in the reference capture.

### 4. Ammo reserve at 0
`ReadInventory()` read guessed offsets (`cItem+0x02`, `type == 2`) that do not match. A one-shot probe (`probe:` in the log) now dumps the weapon structure and the item table to fix the offsets from real values.

### 5. IW4L viewmodel all white (Rust runtime)
Diagnostic in `E:\re4z\build\iw4l-artifacts\logs\latest.log`:
- `fpv prepared ... refused=2966`: many T5 materials refused (`material has no sorted ordinal`).
- The M1911 passes admission (9 draws, 4 materials, 8957 vertices).
- The colour product does contain the 9 FPV draws (`probe index census: fpv=n=9`).
- But the output pixels are **100% white** (median RGB 255,255,255 over 13% opaque pixels, bbox x 662..1758 y 541..1079): geometry is correct, the texture is not.
- `draw_exact_colour: product_empty`: the exact submit does not see the draws.
Temporary diagnostic added in `render_frontend/.../xmodel_lane.rs` (log `fpv_plan draws=... materials=... surfaces=... decoded=...`).

### Runtime: BO1 viewmodel vs RE4 viewmodel
Until the IW4L viewmodel is fixed, RE4's native viewmodel (Leon + M1911, correct textures) remains the correct render. The two must not overlap.

### IW4L viewmodel: detailed diagnostic state (12/11)
The runtime alone (without RE4) produces a 1920x1080 frame where:
- 269 241 opaque pixels (13% of the image), **100% white** (median RGB 255,255,255);
- 218 grey shades, R == G == B everywhere -> not saturation but a missing texture (white placeholder);
- bbox x 662..1758, y 541..1079: the viewmodel silhouette is correct, only the texture is missing.

Verified OK in the runtime:
- `fpv prepared ... models=94 rigs=2100`: the M1911 rig is mounted;
- `fpv plan: draws=16 visible=true placement_ok=true geometry_ok=true`;
- `rebuild_xmodel: fpv_plan draws=9 materials=4 surfaces=9 decoded=8957`;
- `product kind census: colour: n=9 fpv=9 techs={9:9}`: the colour product does contain the 9 FPV draws, technique 9 (`GFX_DRAW_METHOD_LIT_BEGIN`);
- no submit refusal (`viewmodel draw refused` absent), pipelines ready (`exact pipelines: working set=739`).

What is missing: the **T5 colour map of the viewmodel is not bound to the bind group**. The `SmodelPassMaterial` produced by `render_anim/src/anim/fpv_material.rs` does carry `color: Some(handle)`, but GPU binds resolve textures through the T5 ports (`SamplerSource::MaterialTexture`), not through the FPV smodel material.

### Bind of the viewmodel: precise trace (12/11)
`resolve_uploaded_texture_binds` traces the textures bound for the FPV draws:
```
#0 material image_id=1013 name=~-gmtl_t5_weapon_m1911_mp_c  name_hash=A0AB1041 register=0
#1 material image_id=1012 name=mtl_t5_weapon_m1911_n           name_hash=59D30D0F register=1
#2 material image_id=1011 name=~~-gmtl_t5_weapon_m1911_mp_s-~bffff598 register=2
#3 material image_id=638  name=weapon_camo_off                register=3
#4 material image_id=822  name=~$white-rgbl                  register=4
#5..#9  material image_id=2969.. name=viewarm_usa_ciawin_standard_* (the player's arms)
```
So **the right textures are bound** (M1911 colour map at register 0, normal map at register 1, etc.).

Validation test (`IW4L_DEBUG_TINT=1`): the M1911 colour map is tinted magenta at runtime (`bridge: tinted 1 viewmodel colour map(s) magenta`) and the render **stays white** -> the pixel shader does not sample the bound texture.

Most likely cause: the shader used by the XModel pass is a T5 port "lit xmodel" whose **colour map comes from the vertex/constant buffer** rather than the texture bind group, or the packed constants do not contain the right slots. Lead: `render_anim/src/draw_build.rs:84` -> `lit_xmodel_pass_material()` sets `color: None` (XModels have no colour map in their material), while FPV (`fpv_material.rs:129`) sets `color: Some(handle)` - the two paths do not converge on the same bind.

To fix: either rewrite the XModel port/shader to sample the colour map of `SmodelPassMaterial`, or route FPV through a T5 port whose `local_samplers` carry the viewmodel textures.

### Diagnostic controls added to the runtime (12/11)
- `IW4L_BRIDGE_LIGHT=<0..255>`: bridge lighting atlas level (default 150; was 255 = white, which washed out the colour).
- `IW4L_BRIDGE_PROBE=<0..255>`: bridge reflection cubemap (default 64).
- `IW4L_BRIDGE_HUD=1`: re-enables the runtime HUD (disabled by default in bridge, the ASI draws the BO1 HUD alone).
- `IW4L_DEBUG_TINT=1`: tints the viewmodel colour map magenta.
- `IW4L_PRODUCT_LOG=1`: colour/light/emissive product census.

Fixes already applied to the runtime (12/11):
- `bridge.rs::spawn_bridge_lighting`: the lighting atlas was filled with pure (255,255,255), which washed out the viewmodel colour. Default value 150 (`IW4L_BRIDGE_LIGHT`), reflection cubemap 64 (`IW4L_BRIDGE_PROBE`).
- **Runtime HUD disabled in bridge** (`set_bridge_hud_visibility`, Startup): the ASI draws the BO1 HUD alone. Before, the IW4 HUD showed on top (perk badges in the centre, IW4 ammo counter "152/0", score "0"). `IW4L_BRIDGE_HUD=1` re-enables it.
- Diagnostics added (to keep or remove in verbose mode): `render_frontend/.../xmodel_lane.rs` (fpv_plan draws=...), `render_frontend/.../frame_products.rs` (`log_product_kind_census`), `render_gpu/.../prepare_camera.rs` (viewmodel draw refusals).

## RE4 offsets corrected (12/11) - source: MW2RE4 / re4_tweaks

`F:\Desktop\Divers\Deep\MW2-RE4-main\host\src\Re4Game.h` holds the validated RE4 memory layout. My guessed offsets were wrong:
| field | before (wrong) | correct |
|---|---|---|
| `cEm::hp` | +0x320 | **+0x324** |
| `cEm::be_flag` | +0x324 | **+0x004** (cUnit::be_flag) |
| `cUnit::hp` | +0x320 | **+0x324** |
| `cUnit::be_flag` | +0x320 | **+0x004** |

## Bridge: Leon's routine and weapon number (12/11)

- Leon's animation routine is `0x686C40` (`BulletHit`), called from `0x686A30` (`LeonRoutine`), dispatched from `0x689AF0` (`EmMgrUpdate`).
- `weapon_no` lives at `g+0x4FB0` (uint8), not `g+0x4FC0` (which is the room ID).
- `BulletHit` signature: `BulletHit(uint8_t weapon_no, cEm* target, uint8_t flags, Vec3* hitpos, ...)`.

## BO1 zombies HUD: reference layout (13/11)

From the `hud_zombie` assets extracted via OpenAssetTools and the `_zombiemode*.gsc` scripts:

- Score: bottom right, on the blood splat `scorebar_zom_1` (512x64, white+alpha, tinted by the shader).
- Perk badges: bottom left, 32x32, pitch 25.5 (ref 704).
- Round number: chalk red, below the perk badges (or above, per the reference capture).
- Ammo counter: bottom right, above the score.

## Bugs found in game (13/11)

### Zombies invisible except for their hat (runtime active)
When the runtime is active, zombie bodies disappear but their hat (or hood) stays. Suspected cause: rendering/culling path of the runtime (camera), not the freeze (the freeze is also active without the runtime, where zombies stay visible). Diagnostic: `BO1Z.ini` with `FreezeCap=0` / `IntroFreeze=0`.

### Intermittent damage (sometimes does not kill)
Some shots log `BulletHit returned 1` without a following `damage:` line. To trace whether it is host-side or target-side.

### HUD
- MW2 HUD did not disappear (`cg_draw2D 0` via env had no effect); fixed via `--cmds` after `force_match_start`.
- Right-side elements (score, ammo, dpad) were plain numbers without the BO1 background; score splat now drawn.
- Crosshair and D-pad "UP/DN/LT/RT" letters removed per user request.

## 17/11 - Sting perk + crosshair/D-pad removal + round change mp3 + diagnostics

User feedback (21:25): 2 captures provided (exemple bo1.png = BO1 reference, in-game screen).
- Perks: no bottle-drinking animation, wrong gulp sound, wrong jingle (full jingle instead of the "refrain" heard while drinking), round jingle still missing (user provided "Call of Duty Zombies - Round Change Sound Effect.mp3").
- Some zombies still invisible; damage intermittent.
- Remove the fake center crosshair and the random letters on the left (LP/DN/RT).

Done:
- Perk buy: plays the perk sting (bo1_sting_juggernog/doubletap/quickrevive) immediately, on the MCI channel (survives quips). No more gulp (it was the wrong sound); the full jingle no longer plays on buy (reserved for the machine). Quick Revive: sting extracted from the tail of the jingle (2.5 s). Speed Cola: no sting in the .ff -> silent.
- Round: bo1_round_change.mp3 (user-provided) plays at round end (MCI).
- Center crosshair removed; D-pad "UP/DN/LT/RT" block removed.
- PROJET-NOTES + BO1Z.ini (FreezeCap/IntroFreeze) still in place for the invisibility test.

Not done (to come):
- Bottle-drinking animation (RE4 viewmodel): requires reverse-engineering of the player animation system (rno0/rno1/rno2) + a bottle model -> big task, deferred.
- Intermittent damage: send an extract of MW2RE4.log with a `shot N ... BulletHit returned 1` with no following `damage:` line.
- Zombie invisibility: test with BO1Z.ini (FreezeCap=0) required to decide.
- Exact HUD calibration (splat, ammo, dpad): current coords are estimates; the BO1 capture provided will allow pixel-accurate adjustment.

## 17/11 - Exact HUD (calibration on the BO1 capture)

Colour-mask analysis of the BO1 capture (exemple bo1.png, 1800x917):
- Perk badges: centre of the owned badge at (193, 768) -> first badge x = 53 ref (already right), Y from 289 to 332 ref (badges BELOW the round number, as in BO1).
- Round number (chalk red): (139, 670) -> ref (51.2, 289): Y from 380 to 289 (ABOVE the badges, no overlap).
- Score: text at (1327..1397, 769..793) -> ref right edge ~547, baseline ~332 (was 603/290, too far right and too high). Score splat repositioned in 8:1 behind the score (x 327..551, y 318..346), tint ARGB(235,105,20,14).
- Popup +N: recalibrated to the score (537, 332).
- Ammo: already bottom-right, kept.
- Crosshair and D-pad "UP/DN/LT/RT" removed.

Left to calibrate: exact ammo (the digits in the BO1 capture were not isolated automatically - resolution/aliasing). To adjust on visual feedback.

Tests requested:
1. HUD: score on the red splat, round number above the badges, no crosshair, no UP/DN/LT/RT letters, perk badge pop still OK.
2. Zombie invisibility: BO1Z.ini with FreezeCap=0 and IntroFreeze=0, runtime ACTIVE.
3. Intermittent damage: extract of MW2RE4.log with a `shot N ... BulletHit returned 1` with no following `damage:`.

## General notes
- Steam (`E:\Steam\steam.exe`) must run before `bio4.exe` (steam_appid 254700).
- GrayWolf: BO1 install = `F:\Games\pluto_t5_full_game`; sound export: `zones:[zone], name_filter:[path/without/extension], kinds:["sound"], audio_format:"wav"`; asset names use `/` + `.wav`; Dempsey = plr0; kill quip pool: `vox_plr_0_kill_close_0..6`.
- Perk jingles: `sound\mus\zombie\perksacola\mus_jugganog/doubletap/revive_jingle` (zone `zombie_temple.ff`); stings: `mus_*_sting`; no Speed Cola jingle/sting in the .ff.
- `cg_draw2D` is a real runtime dvar; set via `--cmds` after `force_match_start`.
- Do not launch the game yourself; test on the user's request.
- Keep notes in French (this file is the English translation for the repo).
