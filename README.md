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

What does not work yet:
- Bottle-drinking animation for perks.
- Pack-a-Punch.
- Real wave spawning driven by the BO1 round budget.
- Special enemies (dogs) every 5 rounds.
- Spawn at the village centre (deferred).
- Some zombies render invisible while the MW2 runtime is active (under investigation).
- Intermittent damage (some shots do not kill) — under investigation.

## Building

The project uses MinGW (i386). From `src/re4z/`, run `build_hudonly.bat` to build `BO1Z.asi`. From `src/mw2re4/`, compile `MW2RE4_new.cpp` with g++ (see the build command in the source). The precompiled MW2 runtime (`iw4l-re4.exe`) is not rebuilt.

## Credits

- **IW4L pipeline** — the Rust/Bevy runtime used for asset loading and compositing.
- **MW2RE4** — the MW2-in-RE4 mod whose architecture (host ASI + runtime bridge, shooting, HUD compositing) this project reuses and is inspired by.
- **GrayWolf** by Kenshin9977 — used to extract Black Ops 1 assets (sounds, images) from the game files.
- Black Ops 1 / Resident Evil 4 — their respective owners.

## Notes

- Notes and journal are kept in French in `PROJET-NOTES.md`.
- BO1 assets are extracted from the user's own Black Ops 1 install via GrayWolf; they are not redistributed.
