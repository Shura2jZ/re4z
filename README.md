# re4z

An experimental mod that turns Resident Evil 4's Mercenaries mode into a Black Ops 1 Zombies-style experience.

RE4's world, enemies, story, items, merchant, doors and QTEs stay untouched. Only the weapons (models, hands, animations, sounds), the shooting mechanics (rate of fire, recoil, aiming, ammo, damage) and the HUD are replaced with authentic Black Ops 1 (T5) assets.

## Status

**Work in progress — far from finished.** This is a research/hacking project, not a release. Expect rough edges, missing features and frequent breakage.

What works so far:
- BO1-style points (50 per kill, 10 per hit) with the yellow "+N" popup.
- BO1 round system (health scaling, simultaneous-zombie cap, intermission).
- BO1 HUD elements drawn over the frame (score on the blood splat, round number, perk badges, ammo).
- Perk-a-Cola buy menu (F8) with Juggernog / Quick Revive / Speed Cola / Double Tap.
- Tank Dempsey kill voice lines.
- Authentic BO1 weapon sounds and HUD fonts extracted from the game files.
- Pack-a-Punch: real BO1 machine model, 5000 points, upgrades the held weapon.
- Mystery box: real BO1 jingle, animated lid, 14 weapon models (Ray Gun included)
  cycling above the box, press F to take the chosen gun.
- Chalk wall-buy drawings taken from the BO1 game files, fixed in the world.
- World objects use BO1 models placed in RE4's 3D scene with the game's depth buffer.

What does not work yet:
- Bottle-drinking animation for perks.
- Real wave spawning driven by the BO1 round budget.
- Special enemies (dogs) every 5 rounds.
- Spawn at the village centre (deferred).
- World models drift slightly when the camera moves (under investigation).
- Some zombies render invisible while the MW2 runtime is active (under investigation).
- Intermittent damage (some shots do not kill) — under investigation.

## Building

The project uses MinGW (i386). From `src/re4z/`, run `build_hudonly.bat` to build `BO1Z.asi`. From `src/mw2re4/`, build the host with:

```
g++ -m32 -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ \
    -o MW2RE4_new.asi MW2RE4_new.cpp -lgdi32 -luser32 -lwinmm
```

`-lwinmm` is required for the MCI sound channels. The precompiled MW2 runtime (`iw4l-re4.exe`) is not rebuilt. Loaded BO1 map: `zombie_cod5_prototype` (Nacht der Untoten).

## Credits

- **IW4L pipeline** — the Rust/Bevy runtime used for asset loading and compositing.
- **MW2RE4** — the MW2-in-RE4 mod whose architecture (host ASI + runtime bridge, shooting, HUD compositing) this project reuses and is inspired by.
- **re4-boshjerns-sdk** by boshjerns — the GX camera-matrix and depth-tested scene-drawing technique used to place models in RE4's 3D world (MIT).
- **GrayWolf** by Kenshin9977 — used to extract Black Ops 1 assets (models, textures, sounds) from the game files.
- Black Ops 1 / Resident Evil 4 — their respective owners.

## Notes

- Notes and journal are kept in French in `PROJET-NOTES.md`.
- BO1 assets are extracted from the user's own Black Ops 1 install via GrayWolf; they are not redistributed.
