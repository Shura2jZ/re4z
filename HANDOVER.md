# HANDOVER — Projet RE4 → Mode Zombies Black Ops 1

Document de reprise pour un nouvel agent. Lire en entier avant d'agir. Toutes les notes projet sont en **français**.

## 1. Objectif

Transformer le mode Mercenaires de Resident Evil 4 (village, r400–r4FF) en mode Zombies Black Ops 1 :
- Spawn/vagues par manches, ennemis spéciaux toutes les 5 manches
- HUD BO1 exact, +10 jaune par hit
- Perks buvables avec animation, Pack-a-Punch
- Voicelines Tank Dempsey
- Priorités utilisateur (ordre) : (1) HUD BO1 exact, (2) voicelines Dempsey, (3) perks buvables + icône qui pop, (4) vagues réelles. Le spawn au milieu du village est **reporté** (trop coûteux).

## 2. Architecture

- **RE4 Mercenaires** (rooms r400–r4FF, r400 = village) fournit le monde + l'IA. `BO1Z.asi` gèle les surplus, scale les HP, compte les kills, dessine le HUD BO1 (hook D3D9).
- **`MW2RE4.asi` (hôte)** fournit le FPS MW2, les dégâts, le compteur de hits exporté (`MW2RE4_GetHits`), et les effets perks (`MW2RE4_SetPerks`).
- Le monde visible = village RE4 (confirmé par frame).
- Injection via `.asi` chargés par le runtime `iw4l-re4.exe` (MW2). Ne pas utiliser le client IW4X directement.

## 3. Emplacements

| Élément | Chemin |
|---|---|
| Racine projet | `E:\re4z` |
| Jeu | `E:\re4z\game\Bin32` (bio4.exe + `BO1Z.asi` + `MW2RE4.asi`) |
| Source BO1Z | `E:\re4z\src\re4z` |
| Source hôte | `E:\re4z\src\mw2re4` |
| Runtime (ne pas rebuild) | `E:\re4z\game\Bin32\MW2RE4\iw4l-re4.exe` (134 Mo) |
| Sons | `E:\re4z\game\Bin32\re4z\assets\sounds\` |
| Notes projet (FR) | `E:\re4z\PROJET-NOTES.md` |
| Install BO1 (GrayWolf) | `F:\Games\pluto_t5_full_game` |
| Exports GrayWolf | `C:\Users\marou\AppData\Local\GrayWolf\mcp\` |

## 4. Build

- BO1Z : `E:\re4z\src\re4z\build_hudonly.bat` (MinGW i386 `E:\msys64\mingw32\bin`, g++ -m32 -std=c++17). Produit `BO1Z_new.asi` → renommer en `BO1Z.asi` dans `Bin32`.
- Hôte : `g++ -m32 -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ -o MW2RE4_new.asi MW2RE4_new.cpp -lgdi32 -luser32` (depuis `src\mw2re4`). Exporter `MW2RE4_GetHits` et `MW2RE4_SetPerks` (vérifier avec `objdump -p`).
- Ne laisser que `BO1Z.asi` + `MW2RE4.asi` dans `Bin32` (le loader charge tous les `*.asi`).

## 5. Contraintes strictes

- Ne pas utiliser le client IW4X directement ; injecter via le `.ff` que le runtime charge.
- Ne pas rebuild le runtime (134 Mo pré-compilé).
- Suspendre l'automation clavier si VALORANT au premier plan.
- Un seul runtime MW2 doit tourner (mutex singletons en place).
- Notes/réponses en français.
- Comportement caveman non requis pour ce handover (texte normal).

## 6. État actuel (ce qui marche)

- Points BO1 réels : `MW2RE4_GetHits` exporté et lu par `zombies.cpp` (delta) ; points = kills×50 + hits×10 − dépensés ; popup jaune "+N" à côté du score sur tout gain (fusion hit+kill, anim `score_highlight`).
- FPS diagnostics : `fps: host(RE4)=X mw2=Y | composite_ms=Z | mw2 frame age=Nms`.
- INI : `Resolution=960x540`, `FpsCap=60`, `IW4L_PIPELINED_RENDERING=1`.
- Sons extraits (GrayWolf, install bo1 = `F:\Games\pluto_t5_full_game`) dans `Bin32\re4z\assets\sounds\` : `bo1_dempsey_kill_close_0..6.wav`, `bo1_jingle_juggernog/doubletap/quickrevive.wav`, `bo1_sting_juggernog/doubletap/quickrevive.wav`, `bo1_round_change.mp3` (fourni par l'utilisateur), `bo1_perk_drink.wav`, round start/complete, armes.
- Code `zombies.cpp` : achat perks (F8 menu, touches 1–4, reset au changement de stage, `Zombies_PerkInfo` exporté), voicelines sur kill (rotation 7, throttle 2,5 s), champs `perkOwned/perkPop/perkMenu` dans `zombies.h`.
- Code hôte : `cg_draw2D 0` via `--cmds` runtime, `MW2RE4_SetPerks` exporté, Jugg dans le wrapper `ScaleDamage` (×0,4 dégâts reçus), SpeedCola sur `balance.reloadMs` (÷2).
- Overlay : remplacement des toggles F1–F4 par `perkOwned` (pop 0,6 s scale+alpha), menu Perk-a-Cola (PERK-A-COLA, 4 lignes coût, BUE si owned, F8 : FERMER), score sur splat `scorebar_zom_1` (calibré), numéro de manche au-dessus des badges, crosshair et lettres D-pad retirés, HUD MW2 caché.
- Sons multi-canaux via MCI (`MciPlay`), `PlaySoundA` réservé aux quips de kill.
- `BO1Z.ini` (à côté de bio4.exe) : `[BO1Z] FreezeCap` et `IntroFreeze` (défaut 1) pour isoler le bug d'invisibilité.
- `PROJET-NOTES.md` à jour.

## 7. Ce qui reste à faire (par priorité)

### A. HUD BO1 exact (priorité 1)
- Calibrer au pixel les positions restantes (munitions exactes) sur la capture BO1 fournie (`exemple bo1.png`).
- Ajouter les fonds BO1 manquants (ammo counter box, éventuellement dpad art).
- Vérifier que `cg_draw2D 0` (via `--cmds`) masque bien tout le HUD MW2 ; sinon utiliser `IW4L_CONSOLE_STDIN`.

### C. Perks buvables (priorité 2)
- **Animation de bouteille** (viewmodel RE4) : reverse-engineering de l'anim joueur (`rno0/rno1/rno2`) + modèle de bouteille — gros chantier.
- Effets QuickRevive et DoubleTap côté hôte (actuellement pas d'effet).
- Pack-a-Punch (machine, 5000 pts, arme → ligne `_upgraded` du catalogue).
- Jingle entier sur la machine à perks (actuellement seul le sting joue à l'achat).

### D. Vagues réelles (priorité 3)
- Budget de manche BO1 piloté depuis l'ASI (contrôle du spawn RE4).
- Ennemis spéciaux toutes les 5 manches (chiens) : contrôler le type d'ennemi au spawn (recon `bio4.exe` sur la routine de spawn, pattern `kEmMgr` connu).

### B. Voiclines Dempsey (priorité 4)
- Rendre cohérent avec les événements : dégât reçu → quip approprié, achat perk, low ammo, manche, etc. Catégories plr0 connues dans `fr_zombie_cosmodrome.ff` (kill_crawler, kill_explo, kill_headshot, etc.).
- Pool `kill_close` déjà en place (7 lignes, throttle 2,5 s).

### E. Bugs ouverts
1. **Zombies invisibles sauf le chapeau** quand le runtime est actif. Test avec `BO1Z.ini` (`FreezeCap=0`, `IntroFreeze=0`) pour trancher : si visibles → le gel `be_flag` bit 4 est en cause ; sinon → chemin de rendu du runtime (culling caméra).
2. **Dégâts intermittents** (parfois ne tuent pas). Extraire de `MW2RE4.log` une séquence `shot N ... BulletHit returned 1` sans ligne `damage:` qui suit pour tracer côté hôte ou cible.
3. **Qualité graphique et fluidité du runtime** (hors cap 30 fps) — optimisation.

## 8. Fichiers clés

- `E:\re4z\src\re4z\zombies.cpp` — rounds/hits/points/perks/voicelines/intro/gel
- `E:\re4z\src\re4z\zombies.h` — champs `perkOwned/perkPop/perkMenu`, `Zombies_PerkInfo`
- `E:\re4z\src\re4z\overlay.cpp` — HUD BO1 (splat, ammo, round, perks, popup, menu F8)
- `E:\re4z\src\re4z\game.cpp` — mémoire RE4 (EmMgr, ItemMgr, health, roomId, be_flag)
- `E:\re4z\src\re4z\hud_assets.cpp` — chargement textures DDS
- `E:\re4z\src\mw2re4\MW2RE4_new.cpp` — hôte : perks (`MW2RE4_SetPerks`), Jugg, SpeedCola, `cg_draw2D 0`, `--cmds`
- `E:\re4z\src\re4z\build_hudonly.bat` — build BO1Z
- `E:\re4z\PROJET-NOTES.md` — journal complet (FR)

## 9. Repères mémoire RE4 (bio4 1.1.0)

- `cCoord::pos` = pl+0x94
- `cEm::hp` = em+0x324 ; `be_flag` bit 4 = freeze move
- `EmMgr` = base+0x7FDB04 ; `ItemMgr` = base+0x870FD4
- health = g+0x4FA4 ; roomId = g+0x4F9C ; `DPF_COCKPIT` = bit 15 de g+0x58
- Kinds hôte : 1=arme catalogue, 2=reload, 4=bridge_equip, 5=set ammo ; outcome 1=OK, 3=refus (gaps FPV dual-wield)

## 10. Notes pratiques

- Steam (`E:\Steam\steam.exe`) doit tourner avant `bio4.exe` (steam_appid 254700).
- GrayWolf : install bo1 = `F:\Games\pluto_t5_full_game` ; export sons : `zones:[zone], name_filter:[chemin/sans/extension], kinds:["sound"], audio_format:"wav"` ; les noms d'assets export utilisent `/` + `.wav` ; Dempsey = plr0 ; cat kill quips : `vox_plr_0_kill_close_0..6`.
- Jingles perks : `sound\mus\zombie\perksacola\mus_jugganog/doubletap/revive_jingle` (zone `zombie_temple.ff`) ; stings : `mus_*_sting` ; pas de jingle/sting Speed Cola dans les .ff.
- `cg_draw2D` est un dvar réel du runtime ; réglé via `--cmds` après `force_match_start`.
- Ne pas lancer le jeu soi-même ; tester sur demande de l'utilisateur.

## 11. Première action recommandée

1. Lire `E:\re4z\PROJET-NOTES.md` en entier.
2. Vérifier les builds (`BO1Z.asi`, `MW2RE4.asi`) et les exports (`objdump -p`).
3. Demander à l'utilisateur le test BO1Z.ini (invisibilité) + extrait MW2RE4.log (dégâts) avant d'attaquer le gros (vagues réelles / animation bouteille).
4. Continuer en français, mise à jour de `PROJET-NOTES.md` à chaque étape.
