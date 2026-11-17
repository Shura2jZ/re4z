# re4z

Transformer le mode Mercenaires de Resident Evil 4 en mode Zombies Black Ops 1.

Le monde et l'IA viennent de RE4 (village r400-r4FF). BO1Z.asi gele les surplus, scale les HP, compte les kills et dessine le HUD BO1 (hook D3D9). MW2RE4.asi (hote) fournit le FPS MW2, les degats et les effets perks.

## Structure
- src/re4z/ - source de BO1Z.asi (HUD, perks, voicelines, intro, vagues)
- src/mw2re4/ - source de MW2RE4.asi (hote MW2, degats, perks)
- PROJECT-NOTES.md - journal projet (FR)

## Build
- BO1Z : src/re4z/build_hudonly.bat (MinGW i386, g++ -m32 -std=c++17). Produit BO1Z_new.asi a renommer en BO1Z.asi.
- Hote : g++ -m32 -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ -o MW2RE4_new.asi MW2RE4_new.cpp -lgdi32 -luser32 (depuis src/mw2re4).

## Contraintes
- Ne pas rebuild le runtime iw4l-re4.exe (134 Mo pre-compile).
- Injection via .asi charges par le runtime, pas de client IW4X direct.
- Notes en francais.

Voir PROJECT-NOTES.md pour l'etat detaille et la suite.
