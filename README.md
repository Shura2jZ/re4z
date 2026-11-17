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

What does NOT work yet:
- Bottle-drinking animation for perks.
- Pack-a-Punch.
- Real wave spawning driven by the BO1 round budget.
- Special enemies (dogs) every 5 rounds.
- Spawn at the village centre (deferred).
- Some zombies render invisible while the MW2 runtime is active (under investigation).
- Intermittent damage (some shots do not kill) — under investigation.

## How it works

RE4 Mercenaries (rooms r400-r4FF, r400 = village) provides the world and the AI. `BO1Z.asi` freezes surplus enemies, scales their health per round, counts kills and draws the BO1 HUD (D3D9 hook). `MW2RE4.asi` (host) provides the MW2 first-person view, damage and perk effects. The MW2 runtime (`iw4l-re4.exe`) is a precompiled Rust/Bevy renderer used for assets and compositing; it is not rebuilt.

## Building

- BO1Z: `src/re4z/build_hudonly.bat` (MinGW i386, g++ -m32 -std=c++17). Produces `BO1Z_new.asi`, rename to `BO1Z.asi`.
- Host: `g++ -m32 -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ -o MW2RE4_new.asi MW2RE4_new.cpp -lgdi32 -luser32` (from `src/mw2re4`).

## Notes

- Notes and journal are kept in French in `PROJET-NOTES.md`.
- Do not launch the game yourself; tests are run by the user on request.
- BO1 assets are extracted from the user's own Black Ops 1 install via GrayWolf; they are not redistributed.
