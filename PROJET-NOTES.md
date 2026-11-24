# RE4Z — Notes projet (cap corrigé)

État : 29/10/2026.

## Objectif réel (rappel, comme MW2-RE4)

Porter **Black Ops 1 dans Resident Evil 4** exactement comme MW2-RE4 porte MW2 :

- **RE4 reste original** : monde, Ganados, histoire, items, marchand, portes, QTE,
  contrôles de base. **On ne remplace pas les ennemis par des zombies BO1.**
- **BO1 remplace seulement** :
  1. **les armes** : modèles, mains, animations, sons (vrais assets T5) ;
  2. **les mécaniques de tir** : cadence, recul, visée, munitions, dégâts ;
  3. **le HUD** : le vrai HUD BO1 (T5), pas une imitation.
- Architecture : **runtime IW4L (Rust)** = rendu des armes + HUD T5 ;
  **ASI RE4** = pont gameplay (entrées, caméra, dégâts, inventaire).

## Ce que fait MW2-RE4 (lu dans son code source)

- `iw4L` fait tourner la **vraie simulation MW2** (déplacement, armes, tir, HUD, audio).
- `MW2RE4.asi` :
  - publie le monde/collision RE4 vers le runtime ;
  - lit la caméra/entrées et les renvoie au runtime ;
  - composite l'image MW2 sur RE4 (D3D9 Present) ;
  - **traduit chaque tir MW2 en `BulletHit` RE4** (VA 0x686C40) avec le numéro
    d'arme RE4 correspondant → RE4 gère le sang, le stagger, les dégâts, la mort ;
  - mappe l'inventaire RE4 (arme équipée, munitions de la mallette) sur l'arme MW2 ;
  - masque les entrées RE4 (PadRead) pendant que MW2 a la main.

## Écart actuel (pourquoi ça ressemble à une « couche »)

1. `iw4l bridge` **contourne la simulation** : il ne fait que poser le viewmodel
   T5 en offscreen et l'écrire dans la mémoire partagée. Pas de tir simulé,
   pas d'arme qui change, pas de HUD T5.
2. Le HUD affiché est un **HUD zombies dessiné par l'ASI** (`zombies.cpp`) à
   partir de la mémoire RE4 — ce n'est pas le HUD BO1.
3. Les tirs restent **ceux de RE4** (l'arme de Leon cachée tire) ; le viewmodel
   ne fait que l'animation visuelle.
4. Une seule arme codée en dur (M1911).

## Contrainte dure

`iw4L` ne possède **que le chargement des assets T5** (`fastfile_t5`). Toute la
simulation (mouvement, armes, HUD, audio) est **IW4 uniquement** (`movement_iw4`,
`weapon_iw4`, `hud_iw4`…). MW2-RE4 marche parce que la sim MW2 existe déjà dans
iw4L. Pour BO1, cette sim **n'existe pas** : il faudrait porter la logique de jeu
T5 dans iw4L (chantier de plusieurs mois, incertain).

## Chemin réaliste (celui qu'on garde)

- **ASI = propriétaire des armes et du tir** :
  - cadence, recul, visée, munitions, dégâts par arme BO1 ;
  - tir → `BulletHit` RE4 (mêmes dégâts/sang/stagger que RE4) ;
  - mapping arme RE4 équipée → arme BO1 (modèle + stats + son).
- **IW4L = rendu** :
  - viewmodel T5 réel (modèle + mains + animations) ;
  - **vrai HUD T5** rendu offscreen puis composité (remplace `zombies.cpp`).
- **RE4 = monde/ennemis/histoire** inchangés.

## Ordre de travail

1. Pont de tir réel : ASI détecte le tir, applique la cadence BO1, appelle
   `BulletHit` RE4 avec le bon numéro d'arme, log des touches.
2. Désactiver le tir natif RE4 (masque d'entrée) pour que BO1 soit seul maître.
3. Mapping arme équipée RE4 → arme BO1 (viewmodel + stats).
4. Sons d'armes BO1.
5. Vrai HUD T5 (iw4L) à la place du HUD zombies.
6. Recul/visée/ADS BO1.
7. Nettoyage : retirer le faux HUD et le sprite statique.

## Progres recents (29/10/2026)

### Tir BO1 vers RE4 (implemente)
- shooting.cpp : l'ASI possede le tir (cadence par arme), neutralise le tir natif de Leon, et route les degats dans BulletHit RE4.
- Log verifiable : shot N: weapon 0x.. -> BulletHit X. F10 desactive le pont.

### HUD - premiers assets T5 reels
- hud_assets.cpp : chargeur DDS (DXT5/DXT1/DXT3/A8R8G8B8).
- Crosshair remplace par le vrai center_cross.dds extrait de common_zombie.ff.
- Assets extraits dans E:\re4z\game\Bin32\re4z\assets\hud\.

### Outils
- Universal Moder installe (skills charges dans OpenCode, um kb search fonctionnel).
- pyyaml + Pillow + numpy installes.

## Prochaines etapes
1. Extraire plus d'assets T5 HUD (icones d'armes, textures de sante, etc.)
2. Ameliorer le HUD (couleurs BO1, layout, polices)
3. Extraire les sons d'armes BO1
4. Mapping arme equipee RE4 vers arme BO1 (viewmodel + sons)

## Progres (session 2)
- HUD assets T5 : crosshair (center_cross.dds) + icone arme (hud_colt.dds) deployes et rendus.
- Chargeur DDS (DXT5/DXT1/DXT3/A8R8G8B8) dans hud_assets.cpp.
- Tir BO1 vers RE4 : cadence par arme, neutralisation tir natif, degats via BulletHit.
- Outils : Universal Moder (skills + um kb), pyyaml + Pillow + numpy.
- Prochaine etape : sons d'armes BO1, mapping armes multiples, amelioration HUD.

## Progres (session 3)
- Correction crosshair : accolade manquante (le code etait dans le bloc if(!bo1)).
- Icone d'arme : vrai hud_colt.dds (et autres icones deployes).
- Recul : camera pitch/yaw sur tir, decroissance.
- Assets deployes : center_cross, hit_direction, hud_colt, hud_spas12, hud_uzi, hud_m14, hud_cz75, hud_rpg.
- ASI rebuild : 634687 bytes.
- Prochain : sons d'armes (extraction FMOD complexe), mapping armes multiples, polices BO1.

## Progres (session 4)
- Correction tir : thunk hook ne neutralise que si bridge ON (sinon tir natif RE4 passe).
- Correction crosshair : accolade manquante (code etait dans if(!bo1)).
- Icone d'arme : vrai hud_colt.dds (et autres icones).
- Recul : pitch/yaw sur tir, decroissance.
- Sons : extraction FMOD complexe, pas de WAV disponibles. A investiguer (Greyhound/OAT extension).
- ASI rebuild : 635143 bytes.
- Prochain : sons d'armes, mapping armes multiples, polices BO1, ADS.

## Progres (session 5)
- ADS : FOV reduit a 45 deg quand clic droit (vis�e).
- Sons : extraction FMOD complexe, pas de WAV disponibles. A investiguer (Greyhound/OAT extension).
- ASI rebuild : 635xxx bytes.
- Prochain : sons d'armes, mapping armes multiples, polices BO1, ADS viewmodel offset.

## Progres (session 6)
- Sons : extraction FMOD complexe. OAT ne les extrait pas. Greyhound est GUI. Note T5 documente la route (resident LoadedSound, MS ADPCM/WMAv2).
- Beep() ajoute comme feedback audio temporaire (800 Hz, 30 ms).
- ADS : FOV reduit a 45 deg quand clic droit.
- Recul : pitch/yaw sur tir, decroissance.
- ASI rebuild : 635143 bytes.
- Prochain : extraction sons (necessite extension custom ou Greyhound interactif), mapping armes multiples, polices BO1, ADS viewmodel offset.

## Progres (session 6)
- ADS viewmodel : offset applique quand clic droit (shift down/right).
- ADS ASI : detection RMB, envoi event 3/4 au runtime.
- Sons : extraction FMOD complexe, beep placeholder. Route documentee.
- ASI + IW4L rebuild OK.
- Prochain : mapping armes multiples, polices BO1, sons (extracteur custom).

## Progres (session 7)
- Mapping armes : find_weapon utilise maintenant un match partiel. Mapping RE4 itemId -> T5 viewmodel name (m1911, kiparis, mac11, m16a1, cz75, asp, rpg7, knife).
- Switching dynamique : le runtime lit weapon_id depuis le shmem et respawn le viewmodel quand l'arme change.
- ASI ecrit weaponId dans le shmem (bytes 12-15) a chaque frame.
- ADS : FOV reduit + offset viewmodel.
- Recul : pitch/yaw sur tir.
- Sons : Beep() placeholder. Extraction FMOD a faire.
- ASI + IW4L rebuild OK.

## Progres (session 7 - correction F9 crash)
- F9 guard : toggle uniquement si joueur valide (GetPlayer + IsRangeReadable).
- g_forceInGame : auto-disable si joueur/game invalide.
- Overlay_Render : ne rend le HUD que si le joueur est valide.
- Bo1Guest_Render : validation des dimensions shmem, stride, null checks.
- UploadFrame : validation stride.
- ASI rebuild : 635xxx bytes.

## Progres (session 8)
- ADS offset reduit (0,-20,15 au lieu de 0,-50,30) pour ne pas faire disparaitre l'arme.
- Icone d'arme : taille corrigee (64x32 au lieu de 48x48) pour respecter le ratio 2:1.
- ASI + IW4L rebuild OK.
- Prochain : sons d'armes (extracteur FMOD), mapping armes multiples (viewmodel), polices BO1.

## Progres (session 9)
- ADS offset reduit (0,-5,5) pour ne pas faire disparaitre l'arme.
- Icone d'arme : taille corrigee (64x32).
- Weapon mapping : weaponNo au lieu de itemId pour icones et viewmodel.
- Logging : HUD log inclut weaponNo, bridge log inclut weapon name.
- Prochain : analyser les logs pour corriger le mapping armes, polices BO1, sons.

## Progres (session 10)
- Correction ADS offset : (0,-2,2) pour ne pas faire disparaitre l'arme.
- Correction weapon icon : taille 64x32 (ratio 2:1).
- Correction shooting : utilise weapon (mapped) au lieu de ReadWeaponNo() (qui retournait toujours 0x20).
- Correction mapping : base sur itemId au lieu de weaponNo.
- Fallback viewmodel : m1911 si l'arme n'est pas trouvee.
- Prochain : sons d'armes, polices BO1, affiner mapping armes.

## Progres (session 11)
- Correction ADS offset : (0,-2,2) pour ne pas faire disparaitre l'arme.
- Icone d'arme : taille 64x32, mapping par itemId.
- Mapping armes : utilise itemId au lieu de weaponNo (qui etait toujours 0x01).
- Fallback viewmodel : m1911 si l'arme T5 n'est pas trouvee.
- Debug logging supprime (sauf HUD init).
- ASI rebuild : 634xxx bytes.
- Prochain : sons d'armes, polices BO1, affiner mapping armes.

## Progres (session 12)
- Correction weapon icon : taille 64x32, mapping par itemId.
- Correction weapon switch : log itemId + icone mapp�e.
- Debug logging supprime (sauf weapon switch log).
- ASI rebuild : 634xxx bytes.
- Prochain : sons d'armes, polices BO1, affiner mapping armes.

## Progres (session 13 - sons)
- Sons : extraction FMOD complexe. OAT ne les extrait pas. Greyhound est GUI sans CLI. Route documentee dans note T5.
- Beep(800,30) comme feedback audio temporaire.
- Sons stockes en FMOD soundbanks dans les fastfiles (MS ADPCM + WMAv2).
- Extraction necessite un extracteur custom (walker alias lists + SoundFile refs + reconstruction conteneur codec).
- ASI rebuild : 635143 bytes.
- Prochain : extracteur sons custom, mapping armes, polices BO1, ADS viewmodel.

## Progres (session - sons)
- Sons : PlaySoundA avec gunshot.wav genere (placeholder). Lien winmm ajoute.
- Extraction FMOD complexe : route documentee, necessite extracteur custom.
- ASI rebuild : 635xxx bytes.

## Progres (session - sons d'armes)
- Sons d'armes : extraits 41 sons WaW zombies via GrayWolf MCP.
- Deploye : wpn_colt1911_mn.wav, wpn_357_mn_shot.wav, wpn_mp40_st_f.wav, wpn_thompson_st_f.wav, wpn_m1garand_mn.wav, wpn_bar_st_f.wav, wpn_ppsh_st_f.wav.
- PlaySoundA avec fichier specifique par arme (SND_FILENAME | SND_ASYNC).
- Mapping : handgun->colt1911, magnum->357, shotgun->bar, rifle->m1garand, TMP/SMG->mp40.
- Sons WaW (pas BO1 authentique) mais fonctionnels.
- ASI rebuild : 635xxx bytes.
- Prochain : polices BO1, mapping armes (viewmodel), sons BO1 authentiques via extracteur custom.

## Sons authentiques BO1 (extrait 30/10/2026)

Sons de tir standards extraits de common_mp.ff via Tom Crowley BO Sound Tool : Colt (m1911), AK47, M14, Uzi, SPAS (flame), CZ75, RPG. Deploys dans game/Bin32/re4z/assets/sounds/bo1_*.wav. shooting.cpp -> SoundFileForItem() mappe l'item id (ItemMgr +0x10) vers le bon fichier. WaW placeholders conserves pour les autres armes.

## Progres (11/11/2026 - HUD BO1 fidele + systeme de manches)

### HUD : polices et layout calibres sur la capture de reference
- Atlas BO1 reels : `fonts/gamefonts_pc.dds` (512x1024, retail) + `fonts/devfonts_bo1.dds` (256x256, dev).
  Tables de glyphes generees dans `bo1font.h` (8 polices + metriques UV/avance exactes).
- Fontes retenues : compteur de manche = `normalFont` (retail) + halo ; score = `boldFont` ;
  popup = `boldFont` ; munitions = `bigDevFont` (seule police avec l'empatture en pied du "1"
  visible sur la reference).
- Layout (espace 704x396 de reference, mis a l'echelle par `refScale`/`uiScale`) :
  badges de perks x 53 + i*25.5, y 289, 19 px ; "17" x 53..99, baseline 353 ;
  score x 569..604, baseline 290 ; popup x 531..564 ; munitions x 519..579, baseline 352.
- Couleurs relevees sur la capture : score (236,232,230), popup (230,230,0),
  chalk du round (54,0,0) = 0.21/0/0 comme dans les scripts BO1.

### Scripts BO1 recuperes (source de verite)
Extraits des fastfiles avec OpenAssetTools (`Unlinker.exe --include-assets rawfile`) :
- `zone/Common/common_zombie.ff` -> `maps/_zombiemode.gsc` (logique de mode),
  `_zombiemode_score.gsc` (points), `_zombiemode_audio.gsc` (musique), `_zombiemode_utility.gsc`.
- `zone/Common/zombie_temple.ff` (Ascension) -> `maps/zombie_temple_spawning.gsc`.

### Systeme de manches BO1 (formules reelles, pas de table inventee)
`zombies.cpp` :
- `Zombies_RoundSize()` = `default_max_zombie_func()` (_zombiemode.gsc:2482) :
  round 1 = 25% du budget IA, <3 = 30%, <4 = 50%, <5 = 70%, <6 = 90%, puis 100%.
  Budget = `zombie_max_ai` (24) + `zombie_ai_per_player` (6) * multiplier,
  multiplier = round/5 et *= round*0.15 a partir de la manche 10.
- `Zombies_RoundStartKills()` = cumul des tailles de manche (seuil du compteur de kills).
- `Zombies_ZombieHealth()` = `ai_calculate_health()` : 150 + 100 par manche jusqu'a 9,
  puis *1.1 par manche.
- `Zombies_SpawnDelay()` : 2.0 s, *0.95 apres chaque manche, plancher 0.08 s.
- `Zombies_MoveSpeed()` = round * 8.
- Points solo : kill = 50, degat = 10 (arrondi au multiple de 5).
- Table verifiee : manche 1 = 6 zombies, 2 = 8, 3 = 13, 4 = 18, 5 = 24, 10 = 33, 17 = 45, 20 = 60.

### Spawn controle + scaling de la vie
- `ScanEnemies()` parcourt la liste des ennemis vivants (cEmMgr, base + 0x7FDB04).
- Vie des ennemis portee a la valeur de la manche (150, 250, 350, ... 7000 a la manche 30).
- Limitateur d'ennemis simultanes : au-dela de `Zombies_MaxAlive()`, les ennemis sont
  **geles** via `cEm::be_flag` bit 0x4 ("the enemy's move routine is skipped",
  decomp RE4 `st/em_wrap.cpp:469`), puis relaches quand les autres meurent.
- F7 : active/desactive le limiteur (secours si le gel gene la partie).

### Musique de transition de manche (BO1 authentique)
- Noms reels dans les scripts : `mus_zombie_round_start` / `mus_zombie_round_over`.
- Extraits via GrayWolf depuis `zombie_temple.ff` (Ascension), convertis Opus -> WAV 16 bits 48 kHz.
- Deploies : `assets/sounds/bo1_round_start.wav` (14 s), `bo1_round_complete.wav` (9 s).

### Animations
- Le chiffre de manche reste permanent en bas a gauche (comme `chalk_hud1` de BO1,
  qui affiche la valeur numerique au-dela de la manche 10), couleur craie (0.21, 0, 0).
- Pendant l'intermission de 10 s (`zombie_between_round_time`), il pulse en blanc
  comme `chalk_round_over()` (_zombiemode.gsc:3230).
- Suppression de la banniere "ROUND" centrale : elle n'existe pas dans BO1.

### Prochaines etapes
1. Validation en jeu (capture + RE4Z.log) : polices, halo, badges, popup, round.
2. Verifier que le gel des ennemis (be_flag bit 0x4) ne gene pas les scripts de stage.
3. Brancher le vrai HUD T5 rendu par IW4L en remplacement du HUD ASI.
4. Pop-up de points anime (BO1 : deplacement vers le haut-gauche + fondu sur 0.5 s).

## Bugs trouves en jeu le 11/11 (session 19:05-19:09)

### 1. Rectangle rouge autour du chiffre de manche
Le halo additif utilisait `D3DBLEND_ONE` (source/ONE) : le quad entier etait ajoute
en rouge au lieu de suivre le glyphe. Correction : `SRCALPHA/ONE` (l'alpha de la
texture module la couleur). `overlay.cpp` -> `DrawTextGlow()`.

### 2. Aucun popup de points, score a 0
Les compteurs RE4 `pG+0x833C` (kills) et `pG+0x8344` (hits) ne bougent pas : RE4
n'a pas de compteur global de kills. Le compteur est desormais tenu par l'ASI :
`KillTracker` (zombies.cpp) suit la liste des ennemis vivants (cEmMgr) et compte
ceux qui en disparaissent -> kills. Le popup "+50" (BO1 `zombie_score_kill_1player`)
et le score `kills*50 + hits*10` en decoulent.

### 3. Badges de perks absents
`g_perkActive[]` n'etait jamais active (seuls F1-F4 le faisaient). Les 4 badges
sont maintenant actifs par defaut, comme sur la capture de reference.

### 4. Reserve de munitions a 0
`ReadInventory()` lisait des offsets devines (`cItem+0x02`, `type == 2`) qui ne
correspondent pas. Une sonde one-shot (`probe:` dans le log) dumpe maintenant la
structure arme et le tableau d'items pour fixer les offsets a partir de valeurs
reelles.

### 5. Viewmodel IW4L tout blanc (runtime Rust)
Diagnostic dans `E:\re4z\build\iw4l-artifacts\logs\latest.log` :
- `fpv prepared ... refused=2966` : beaucoup de materials T5 refuses
  (`material has no sorted ordinal`).
- Le M1911 passe l'admission (9 draws, 4 materials, 8957 vertices).
- Le produit couleur contient bien les 9 draws FPV (`probe index census: fpv=n=9`).
- Mais les pixels sortis sont **100 % blancs** (median RGB 255,255,255 sur 13 % de
  pixels opaques, bbox x 662..1758 y 541..1079) : la geometrie est correcte, la
  texture ne l'est pas.
- `draw_exact_colour: product_empty` : le submit exact ne voit pas les draws.
Diag temporaire ajoute dans `render_frontend/.../xmodel_lane.rs`
(log `fpv_plan draws=... materials=... surfaces=... decoded=...`).

### Runtime : viewmodel BO1 vs viewmodel RE4
Tant que le viewmodel IW4L n'est pas corrige, le viewmodel RE4 natif (Leon +
M1911, textures correctes) reste le rendu correct. Les deux ne doivent pas se
superposer.

### Viewmodel IW4L : etat detaille du diagnostic (12/11)
Le runtime seul (sans RE4) produit une frame de 1920x1080 dont :
- 269 241 pixels opaques (13 % de l'image), **100 % blancs** (médiane RGB 255,255,255) ;
- 218 nuances de gris, R == G == B partout -> ce n'est **pas** de la saturation
  mais une texture absente (placeholder blanc) ;
- bbox x 662..1758, y 541..1079 : la silhouette du viewmodel est correcte, seule
  la texture manque.

Ce qui est verifie OK dans le runtime :
- `fpv prepared ... models=94 rigs=2100` : le rig M1911 est monte ;
- `fpv plan: draws=16 visible=true placement_ok=true geometry_ok=true` ;
- `rebuild_xmodel: fpv_plan draws=9 materials=4 surfaces=9 decoded=8957` ;
- `product kind census: colour: n=9 fpv=9 techs={9:9}` : le produit couleur
  contient bien les 9 draws FPV, technique 9 (`GFX_DRAW_METHOD_LIT_BEGIN`) ;
- aucun refus de submit (`viewmodel draw refused` absent), pipelines prets
  (`exact pipelines: working set=739`).

Ce qui manque : la **color map T5 du viewmodel n'est pas liee au bind group**.
Le `SmodelPassMaterial` produit par `render_anim/src/anim/fpv_material.rs` porte
bien `color: Some(handle)`, mais le bind GPU resout les textures via les ports
T5 (`SamplerSource::MaterialTexture`), pas via le material smodel du FPV.
C'est un chantier Rust de liaison de pipeline, a traiter separement.

### Bind du viewmodel : trace precis (12/11)
`resolve_uploaded_texture_binds` trace les textures liees pour les draws FPV :
```
#0 material image_id=1013 name=~-gmtl_t5_weapon_m1911_mp_c  name_hash=A0AB1041 register=0
#1 material image_id=1012 name=mtl_t5_weapon_m1911_n           name_hash=59D30D0F register=1
#2 material image_id=1011 name=~~-gmtl_t5_weapon_m1911_mp_s-~bffff598 register=2
#3 material image_id=638  name=weapon_camo_off                register=3
#4 material image_id=822  name=~$white-rgbl                  register=4
#5..#9  material image_id=2969.. name=viewarm_usa_ciawin_standard_* (les bras du joueur)
```
Donc **les bonnes textures sont liees** (color map M1911 au registre 0, normal map
au registre 1, etc.).

Test de validation (`IW4L_DEBUG_TINT=1`) : la color map M1911 est teinte
magenta au runtime (`bridge: tinted 1 viewmodel colour map(s) magenta`) et le
rendu **reste blanc** -> le pixel shader n'echantillonne pas la texture liee.

Cause la plus probable : le shader utilise par la passe XModel est un port T5
"lit xmodel" dont la **color map vient du vertex/constant buffer** et non du
bind group de textures, ou les constantes packees ne contiennent pas les bons
slots. Piste : `render_anim/src/draw_build.rs:84` -> `lit_xmodel_pass_material()`
met `color: None` (les XModel n'ont pas de color map dans leur material), alors
que le FPV (`fpv_material.rs:129`) met `color: Some(handle)` - les deux chemins
ne convergent pas vers le meme bind.

A traiter : soit le port/shader XModel est reecrit pour echantillonner la
color map du `SmodelPassMaterial`, soit le FPV est passe par un port T5 dont les
`local_samplers` portent les textures du viewmodel.

### Controles de diagnostic ajoutes au runtime (12/11)
- `IW4L_BRIDGE_LIGHT=<0..255>` : niveau de l'atlas de lighting du bridge
  (defaut 150 ; avant 255 = blanc, ce qui lavait la couleur).
- `IW4L_BRIDGE_PROBE=<0..255>` : cubemap de reflexion du bridge (defaut 64).
- `IW4L_BRIDGE_HUD=1` : reactive le HUD du runtime (desactive par defaut en
  bridge, l'ASI dessine seul le HUD BO1).
- `IW4L_DEBUG_TINT=1` : teinte en magenta la color map du viewmodel.
- `IW4L_PRODUCT_LOG=1` : census produits couleur/light/emissive.


Correctifs deja appliques au runtime (12/11) :
- `bridge.rs::spawn_bridge_lighting` : l'atlas de lighting etait rempli de
  (255,255,255) pure, ce qui lave la couleur du viewmodel. Valeur par defaut
  150 (`IW4L_BRIDGE_LIGHT`), cubemap de reflexion 64 (`IW4L_BRIDGE_PROBE`).
- **HUD du runtime desactive en bridge** (`set_bridge_hud_visibility`, Startup) :
  l'ASI dessine seul le HUD BO1. Avant, le HUD IW4 s'affichait par-dessus
  (badges de perks au centre, compteur de munitions IW4 "152/0", score "0").
  `IW4L_BRIDGE_HUD=1` le reactive.
- Diagnostics ajoutes (a retirer ou garder en mode verbose) :
  `render_frontend/.../xmodel_lane.rs` (fpv_plan draws=...),
  `render_frontend/.../frame_products.rs` (`log_product_kind_census`),
  `render_gpu/.../prepare_camera.rs` (refus de draw viewmodel).

## Offsets RE4 corriges (12/11) - source : MW2RE4 / re4_tweaks

`F:\Desktop\Divers\Deep\MW2-RE4-main\host\src\Re4Game.h` contient la
disposition memoire RE4 validee. Mes offsets guesses etaient faux :
| champ | avant (faux) | correct |
|---|---|---|
| `cEm::hp` | +0x320 | **+0x324** |
| `cUnit::be_flag` | +0x00 | **+0x04** |
| `cManager` array/count/stride | +0x00/+0x04/+0x08 | **+0x04/+0x08/+0x0C** |
| `cItem::ammo` | (non lu) | **+0x08, valeur << 3** |
| `cItem::num` | +0x02 | +0x02 (ok) |
| `cItem::flag` / `chr` | +0x04 / +0x05 | +0x04 / +0x05 (ok) |
| `cItemMgr` wep/items/count | +0x0C/+0x14/+0x1C | ok |

Consequence : la liste des ennemis vivants etait vide (`alive=0`), donc aucun
kill n'etait compte, le score restait a 0 et la vie des ennemis n'etait pas
scaled. Corrige.

## Backlog demande par l'utilisateur (12/11)

1. **Musique** : couper la musique Mercenaries de RE4 et la remplacer par la
   musique "115" d'Elena Siegmann (Kino der Toten, 1924). Attention : "115" est
   une bande-son de film sous-titree allemande, pas un asset BO1 -> il faut la
   source (DVD/rip) et la rights. Alternative : un asset BO1 deja dans le jeu.
2. **Pistolet laser BO1** : pas un modele T5 dans BO1 ("laser" n'est qu'un effet
   VFX). Les viewmodels T5 dispo sont 34 (voir liste) - il faut choisir un
   remplacant (ex. `t5_weapon_1911_viewmodel` = vrai M1911 BO1, distinct de
   `kiparis` qui est le M1911 de BO2/mp).
3. **Zeus (canon electrique)** : pas dans les assets T5 de BO1 (arme de BO2/WAW).
4. **Animations de course** : poser les clips xanim de course dans le viewmodel
   (le runtime ne pose pour l'instant que l'idle).
5. **Vraie animation de visée (ADS)** : au lieu de retrecir l'arme, poser le
   clip xanim ADS.
6. **Faisabilite MW2RE4** : `E:\SteamLibrary\steamapps\common\Resident Evil 4\MW2RE4
   contient un runtime MW2 complet (`iw4l-re4.exe`, 134 Mo) deja installe et
   fonctionnel, avec `game_folder_black_ops=` dans sa config (donc deja pret a
   pointer vers une install BO1). Le runtime MW2RE4 a le support T5 (`t5:`,
   `fastfile_t5`). C'est la base la plus solide pour un viewmodel BO1 reussi :
   le pipeline MW2 (simulation, ADS, animations, degats) est deja au point.
   Le patch qui le genere est `MW2-RE4-main/runtime-patches/re4-bridge.patch`
   (73 Ko), applique sur le meme `iw4L-master` que nous utilisons.


## Armes BO1 refusees par le runtime (15/11) - diagnostic + correctif

**Symptome** : le menu Guns (F10) liste les armes BO1 (catalogue publie par
`BO1Z.asi`, 126 entrees : lignes registre 1195..1273 = bloc T5 `common_zombie.ff`
+ roster MW2), mais l'equipement echoue : `MW2 refused the request`, outcome 3.
Aucun log `GTA inventory request` pour la ligne 1257 (`ray gun zm`) : le refus
arrive AVANT toute action sim.

**Cause** : `crates/bootstrap/src/gta_bridge/features.rs` (runtime MW2-RE4) :

    1 if bridge.features.catalog.iter().any(|e| e.weapon == request.weapon) => GiveWeapon{..}
    _ => { bridge.features.ack = request.sequence; bridge.features.outcome = 3; return; }

Le catalogue **interne** du runtime est trie equipment d'abord, puis par nombre
de `_` du nom de script, puis par nom, et tronque a 128 entrees
(`export()`). Les armes MW2 ont 1 `_` (`ak47_mp`), les armes T5 zombies en ont
2-3 (`ray_gun_zm_mp`) : les T5 **primaires** sont donc hors des 128 places.
Seules les offhands T5 (`zombie_cymbal_monkey` 1268, `frag_grenade_zm` 1218)
entrent (groupe equipment). `BO1Z.asi` ne remplace que le catalogue **publie**
(menu F10), pas l'interne : les deux listes sont independantes.

**Correctif (cote hote, sans rebuild du runtime)** : la demande `kind=4`
(routee par le patch RE4 vers `bridge_equip`) n'a AUCUN garde-fou catalogue :

    pub fn bridge_equip(&mut self, id, weapon, clip, stock, size) -> bool {
        if frame.player(id).is_none() || frame.combat_facts_for(weapon).is_none() { return false; }
        for other in weapons(&frame, id, WeaponList::Primaries) { if other != weapon { take_weapon(...) } }
        if !has_weapon(...) && give_weapon(...).is_err() { return false; }
        set_bridge_clip_size(weapon, size);
        bridge_ammo(&mut frame, id, weapon, clip, stock);
        switch_to_weapon(&mut frame, id, weapon);
    }

`MW2RE4_new.cpp` : `EquipCatalogWeapon()` envoie `Request(4, row, false, clip,
stock, size)` pour les armes BO1 (lignes >= 1195, inventaire `primary`), et
garde `Request(1, row)` pour les armes MW2 et les grenades. Le runtime lit le
chargeur/la reserve/la taille dans `features.reserved[1..3]`
(`features::BASE + 48/52/56`), que l'hote ecrit deja dans `Exchange()`.

**Munts** (`.ff`, `startAmmo` / `clipSize`, meme regle que le runtime
`spawn_clip_stock` : chargeur = min(startAmmo, clipSize), reserve = le reste) :
| armee | ligne | chargeur | reserve | taille |
|---|---|---|---|---|
| ak74u zm | 1196 | 8 | 0 | 20 |
| ray gun zm | 1257 | 20 | 140 | 20 |
| ray gun upgraded zm | 1256 | 40 | 160 | 40 |

Table generee : `E:\re4z\src\mw2re4\bo1_guns.h` (`gen_bo1_guns.py`).
Hote rebuild : `g++ -m32 -std=c++17 -O2 -shared -static -static-libgcc
-static-libstdc++ -o MW2RE4_new.asi MW2RE4_new.cpp -lgdi32 -luser32`
(MinGW `E:\msys64\mingw32\bin`), deploye sur
`E:\re4z\game\Bin32\MW2RE4.asi` (973 016 o, ancien 964 210 o -> `MW2RE4_prev.asi`).

**Pas de thunder gun** : `common_zombie.ff` (BO1) n'a que le ray gun
(`ray_gun_zm` 1257 / `ray_gun_upgraded_zm` 1256). Le thunder gun est un asset
BO2/WAW, absent du `.ff` BO1.

## Correctif ammo BO1 (15/11) + diagnostic jitter

**PaP "2 balles puis reload"** : ma table donnait `clip = min(startAmmo, clipSize)`, ce qui laissait un
Pack-a-Punch avec 7 cartouches. Les champs `.ff` sont : `clipSize` = chargeur, `maxAmmo` = reserve, `startAmmo` = reserve de depart.
Le runtime applique sa propre regle `spawn_clip_stock` (`clip0 = min(startAmmo, clipSize)`) lors d'un `give`, mais pour un `bridge_equip` on veut un chargeur plein.
Nouvelle formule : `clip = clipSize`, `stock = maxAmmo` (offhands : le count reste le chargeur).
Rebuild hote (973 016 o) redeploye sur `E:\re4z\game\Bin32\MW2RE4.asi`.

**Armes toujours refusees ("MW2 refused the request")** : ce sont exactement les armes listees
`weapon dependency gap` dans le log runtime : `cz75dw_zm`, `cz75dw_upgraded_zm`, `hs10_upgraded_zm`,
`m1911_upgraded_zm`, `pm63_upgraded_zm` - gaps FPV (tag_weapon1 / hands layout). Le runtime les exclut de
`weapon_runnable` -> pas de combat facts -> `bridge_equip` retourne false. Ce sont des handguns dual-wield.
Les armes primaires (AK, FNFAL, Galil, ray gun) passent.

**Jitter / "MW2 bouge de partout"** : l'image MW2 dans la memoire partagee contient le FPV + le HUD sur fond
magenta (le monde MW2 n'est pas dessine). L'hote applique un chroma key magenta -> seules les pixels non-magenta
(FPV arme, HUD) sont recomposites par-dessus le frame RE4 -> double image decalree -> effet de "bouge de partout".
Le monde (les ~400 triangles envoyes par l'hote) n'est pas rendu. Cause probable : le camera/position envoyee
par l'hote au runtime (g_guestCamera) pointe hors du monde, ou l'echelle des coordonnees RE4->MW2 est incoherente.
A verifier : comparer g_guestCamera avec la position MW2 du runtime (la ligne "player=Some((..., [-x, -y, -z]))").

## 16/11 - CAUSE RACINE DU JITTER ET DU "MW2 EN BOUCLE"

**Diagnostic** : "le MW2 se lance en boucle, camera part dans tous les sens, jitter de fou".

**Preuve** (logs runtime `iw4l-artifacts/logs/*.log`) :
- `1794772645.log` contient **deux** lignes `lifecycle: process_exit pid=6264` et `pid=18648`
  -> **deux processus runtime MW2 tournaient en meme temps**.
- `1794774729.log` : deux en-tetes `log: iw4l-artifacts\logs\1794774729.log` (deux runtimes demarres
  dans la meme seconde) et deux `GTA movement activation: epoch=2` **entrelacees** avec des positions
  IW4 differentes ([50,874,-20207] vs [-461,1010,94]) -> les deux ecrivaient dans le meme mapping.

**Cause** : le dossier `E:\re4z\game\Bin32` contenait **4 fichiers .asi** :
`BO1Z.asi`, `BO1Z_old.asi`, `MW2RE4.asi`, `MW2RE4_prev.asi`.
Le loader (`dinput8.dll` = re4_tweaks + Ultimate ASI Loader) scanne `*.asi` du dossier du jeu et charge
**tous** les fichiers `.asi` trouves. Les deux anciennes sauvegardes (`.asi_old` / `.asi_prev`) etaient
donc chargees en plus :
- 2 hotes MW2RE4 -> chacun demarre son propre runtime -> 2 runtimes ecrivant dans le meme
  `Local\MW2RE4_v1` -> la camera et le frame venaient de 2 mondes differents -> **double image
  decalee = jitter + visee qui part partout**, 2 fois les sons MW2, MW2 "qui se lance en boucle".
- 2 BO1Z -> 2 crochets D3D9 + 2 publications du catalogue -> le compte du catalogue alternait
  126/128 (flood "weapon menu: 120 of 128 / 121 of 126" dans le log hote).

**Correctif** :
1. `BO1Z_old.asi` et `MW2RE4_prev.asi` deplaces vers `E:\re4z\backup_asi (hors du dossier du jeu).
   Plus que 2 `.asi` dans `Bin32` : `BO1Z.asi` et `MW2RE4.asi`.
2. Verrou code (le bug peut revenir si l'utilisateur remet un `.asi` de sauvegarde) :
   - `MW2RE4_new.cpp` : mutex `Local\MW2RE4_host_singleton` au DllMain. Si une 2e copie est presente,
     elle ecrit `MW2RE4-double.log` et renvoie FALSE (le loader la saute).
   - `MW2RE4_new.cpp` : `KillOtherRuntimes()` avant `StartRuntime()` : tue tout `iw4l-re4.exe` qui
     n'est pas notre enfant (reliquat d'une session RE4 precedente).
   - `dllmain_hudonly.cpp` (BO1Z) : mutex `Local\BO1Z_host_singleton`, meme regle.

**Note** : la detection "magenta" (frame MW2 vide) avait ete appliquee puis revertie : elle masquait
le symptome au lieu de le traiter. La vraie cause etait le double runtime.

## 16/11 - DEGATS + FPS + HUD

**Degats (cause racine)** : le port MinGW avait remplace le `__declspec(naked)` asm par un stub
`void LifeDownHook() { }` MAIS le thunk `LifeDownSet2` etait TOUJOURS hooké (log: "damage sliders:
LifeDownSet2 hooked at thunk 0x000c71c1"). Donc la fonction qui applique les degats aux ennemis
(HP) et au joueur n'etait plus jamais appelee -> "je les touche j'arrive a les knock au sol mais
ils ne meurent jamais", "je prends des coups mais ma vie ne baisse pas". Le stub remplace la vraie
fonction par un no-op.

Correctif : reimplemente le trampoline en AT&T syntax pour GCC (verifie avec un test standalone :
TRAMPOLINE OK). `pushl 8(%esp); pushl 8(%esp); call ScaleDamage; addl $8,%esp; movl %eax,8(%esp);
jmp *orig`. Les symboles sont pins via `__asm__("MW2RE4_ScaleDamage")` pour eviter la
decoration MinGW i386. Le log attendu : "damage: enemy %p (id 0x%02X) %d -> %d (x%.2f)".

**FPS runtime** :
- `IW4L_PIPELINED_RENDERING=0` -> `1` : pipelined rendering permet au runtime de chevaucher le
  rendu et la readback GPU->CPU. Le code de readback.rs a une garde de sequence pour gerer
  l'ordre (out-of-order safe).
- Suppression de la copie intermediaire `g_pixels` (3.7 Mo/frame) : copie directe shared memory
  -> texture D3D9 (une seule passe au lieu de deux).
- `FpsCap` INI : 30 -> 60 (le cap bridait a 30 ; le runtime mesure ~23 fps, donc le cap n'etait
  pas le facteur limitant, mais c'est correct de pas le brider).
- Instrumentation : le status log affiche maintenant `host frames=N` (fps RE4) et un second log
  `runtime: frame_ms=X capture_ms=Y wait_tick_ms=Z pending=W`. `capture_ms` = latence du
  readback GPU->CPU, `wait_tick_ms` = temps d'attente du tick hôte. Ces deux nombres disent
  immediatement si le runtime est GPU-bound ou tick-starved.

**HUD BO1** :
- Suppression du bloc de perks en double en bas au milieu (overlay.cpp 657-687) -> un seul rang.
- BO1Z.asi recompile + deploye avec la garde singleton et la correction du doublon.


## 16/11 (suite) - FPS : mesure reelle cote host + resolution

**Diagnostic** : la session de 22h21 n'a jamais atteint MW2 ACTIVE (le runtime chargait encore quand
le jeu a ete ferme) : les nouvelles metriques n'ont donc rien mesure. En inspectant l'exe du runtime
deploye, le bloc RuntimeTiming a l'offset 12288 (frame_ms, capture_ms...) N'EST PAS publie par le
runtime precompile : l'instrumentation precedente lisait toujours zero (sleep_timing existe dans la
structure Bridge du runtime mais n'est jamais ecrite en memoire partagee).

**Remplacement** : le host mesure maintenant lui-meme, sans modification du runtime :
- fps RE4 : compteur de frames du hook camera (g_frame),
- fps MW2 : increment de frame->sequence publie par le runtime (lire la memoire partagee),
- composite_ms : temps moyen passe dans DrawOverlay (hook Present) chaque seconde,
- mw2 frame age : age de la derniere frame guest publiee.
Nouvelle ligne de log : \ps: host(RE4)=X mw2=Y | composite_ms=Z | mw2 frame age=Nms\.
Lecture : mw2 ~ host(RE4) = le pacing 1 frame/1 frame fonctionne ; mw2 < host = le runtime bevy
n'arrive pas a suivre (rendu GPU + readback) ; composite_ms eleve = le host ralentit RE4.

**Changements appliques** :
- MW2RE4.asi recompile avec ces mesures (deploye dans Bin32).
- MW2RE4.ini : Resolution 1280x720 -> 960x540 (preset par defaut, -44% de pixels a rendre, lire et
  copier ; l'UPDATE officiel est livre en 960x540). Reversible dans le menu MW2 in-game (row
  MW2 RESOLUTION) ou dans l'INI.
- IW4L_PIPELINED_RENDERING=1 confirme supporte par l'exe (PipelinedRenderingPlugin de bevy compile
  dedans, la variable d'env est lue par le runtime).


## 17/11 - Debut mode zombies BO1 reel : points + intro

**Cahier des charges (utilisateur)** : vrai mode zombies BO1 sur RE4 : spawn au centre du village
apres un ecran noir (pas le demarrage mercenaires avec zombies deja la), vagues par manches,
ennemis speciaux toutes les 5 manches, HUD BO1 exact, +10 jaunes a chaque hit a cote du score,
perks buvables avec animation, Pack-a-Punch, voiclines Tank Dempsey sur kill.

**Phase 1 (fait)** : points BO1 reels + intro BO1.
- MW2RE4.asi : la fonction ScaleDamage (trampoline LifeDownSet2, donc chaque evenement de degat)
  incremente un compteur g_hitCount (cible = ennemi, id 0x10..0x4F) et exporte
  MW2RE4_GetHits (verifie dans la table d'exports).
- BO1Z.asi : zombies.cpp lit le delta du compteur chaque frame (GetProcAddress sur MW2RE4.asi,
  ressaye chaque seconde car l'ordre de chargement des .asi n'est pas garanti). Les points sont
  donc desormais kills*50 + hits*10 comme BO1 solo, et tout gain de points declenche le popup
  jaune +N a cote du score (fusion hit+kill, anim score_highlight BO1).
- Intro BO1 au chargement d'une manche mercenaires (r400-r4FF) : 2.2 s ecran noir total, tous les
  zombies geles (be_flag bit 4, ScanEnemies accepte maxAlive negatif = freeze total), puis 1.3 s de
  fondu. Voile dessine en dernier dans Overlay_Render pour couvrir HUD + monde MW2.
- Journal de calibrage : waypoint: room=%04X Leon=(...) toutes les 2 s en jeu, pour fixer les
  coordonnees du centre du village (teleport a venir).

**Plan des phases suivantes** :
2. Vagues reelles : budget de manche BO1 pilote depuis l'ASI (controle du spawn RE4).
3. Ennemis speciaux toutes les 5 manches (manche de chiens) : necessite de controler le type
   d'ennemi au spawn (recon bio4.exe sur la routine de spawn, pattern kEmMgr connu).
4. HUD BO1 exact (scorebar_zom_1, ammo, armes) - suite de la passe de fidelite.
5. Perks buvables + animation (assets specialty_* deja charges).
6. Pack-a-Punch (machine, 5000 pts, arme -> ligne _upgraded du catalogue deja en place).
7. Voiclines Tank Dempsey (extract du t5 installe sur F:).

## 17/11 - Suite : HUD cache MW2 + voiclines Dempsey + perks buvables

**Priorites utilisateur (ordre explicite)** : (1) HUD BO1 exact, (2) voiclines Dempsey (une par
kill, en test), (3) perks buvables + icone du perk qui pop apres avoir bu, (4) vagues reelles.

**Fait cette session** :
- Sons extraits via GrayWolf (install bo1 = F:\Games\pluto_t5_full_game) vers
  game\Bin32\re4z\assets\sounds\ : bo1_dempsey_kill_close_0..6.wav (pool de quips de kill
  plr0 de fr_zombie_cosmodrome.ff), bo1_jingle_juggernog/doubletap/quickrevive.wav
  (zombie_temple.ff ; PAS de jingle Speed Cola dans les .ff -> boisson seule pour ce perk),
  bo1_perk_drink.wav (gulp_large_00 de fr_zombietron.ff).
- MW2RE4.asi (hote, recompile) :
  - HUD MW2 entierement cache via dvar runtime `set cg_draw2D 0` ajoute a IW4L_SCRIPT_DVARS
    (le HUD a l'ecran est desormais celui de BO1Z.asi).
  - MW2RE4_SetPerks(mask) exporte (bit 1 = Juggernog, 2 = QuickRevive, 4 = SpeedCola,
    8 = DoubleTap) ; Juggernog : degats recus x0.4 dans le wrapper ScaleDamage ;
    Speed Cola : reload /2 sur balance.reloadMs. QuickRevive/DoubleTap : pas d'effet encore.
- BO1Z.asi (recompile) :
  - Voicline Dempsey sur kill : rotation du pool kill_close_0..6, throttle 2.5 s, journal
    `vox:`. (Une par rafale de kills pour l'instant, conforme au test demande.)
  - Perks achetables : menu F8 (PERK-A-COLA, 4 lignes nom + prix, BUE si owned, prix BO1 solo
    Jugg 2500 / QuickRevive 500 / SpeedCola 3000 / DoubleTap 2000), achat touches 1-4, jingle
    + gulp + set MW2RE4_SetPerks. Points = kills*50 + hits*10 - depensés ; reset complet au
    changement de stage. Zombies_PerkInfo exporte pour l'overlay.
  - Overlay : badge perk affiche seulement si achete (F1-F4 restent toggles manuels de test),
    pop d'achat 0.4 s (scale 0.4->1 + alpha), menu perk dessine avant le bloc points popup.

**En attente de test utilisateur** (ne pas lancer le jeu soi-meme) :
- F8 -> menu perks -> acheter (1-4) -> jingle + icone qui pop ; voicline Dempsey par kill ;
  HUD MW2 disparu (cg_draw2D 0) ; LifeDown fix toujours bon ; lire les lignes `fps:` dans
  MW2RE4.log (block RuntimeTiming jamais publie par le runtime -> remplace par fps:).

## 17/11 - Fix sons multi-canaux + splat score + dvar HUD + toggles diagnostic

**Retour test utilisateur (21h)** :
- HUD MW2 PAS disparu (cg_draw2D 0 sans effet via env) ; HUD BO1 OK avec F9 mais les elements
  de droite (score, munitions, dpad) sont des nombres nus sans le fond BO1.
- Menu F8 OK, atouts achetables ; MAIS pas d'animation de pop, pas de jingle (PlaySoundA n'a
  qu'un canal : le gulp coupait le jingle immediatement). Les badges perk sont presents dans
  l'onglet weapons de MW2RE4 et renvoient "MW2 refused the request" (lignes catalogue BO1,
  refusees volontairement - MW2RE4_new.cpp ~L2263) : ne pas acheter via ce menu, acheter
  UNIQUEMENT via F8 + touches 1-4.
- Voix Dempsey OK. Degats OK. Manche : il faut le vrai jingle de changement de manche AVANT le
  son de voix ; numero de manche chevauche les badges perk. Zombies invisibles sauf le chapeau
  quand le runtime est actif. Qualite/fluidite du runtime a optimiser (hors cap 30fps).

**Fait** :
- Sons : MciPlay (winmm MCI, un alias = un canal) pour les sons longs ; PlaySoundA garde les
  quips de kill. Achat perk : gulp (MCI "drink") puis jingle programme +0.8 s (MCI "jingle").
  Manche : round-over sting sur MCI "round" (survit aux quips) ; le round_start sting est
  RETARDE a la fin de l'intermission de 10 s (BO1 laisse le sting round-over se jouer avant).
- Nouveau son : bo1_round_jingle.wav = mus_zombie_round_over (zombie_temple.ff, 9.2 s) ; en fait
  bo1_round_complete.wav exporte avant est deja ce meme sting (taille identique).
- Pop perk : 0.6 s smoothstep, demarre a 5% ; fallback overlay corrige (ne saute plus a 100%).
- Numero de manche descendu (y 353 -> 380 en ref 396) pour ne plus chevaucher les badges perk.
- HUD exact : scorebar_zom_1 (512x64, blanc+alpha, teinte par le shader BO1) desormais DESSINE
  derriere le score (DrawSprite helper : quad texture + blend alpha + modulate). Position
  estimee ref 704x396 : x 416..640, y 266..294, teinte ARGB(235,105,20,14) - A CALIBRER sur
  une capture BO1 (asset introuvable dans les .ff, placement dans hud.menu inaccessible).
- HUD MW2 : `set cg_draw2D 0` ajoute aux --cmds du runtime apres force_match_start (l'env
  IW4L_SCRIPT_DVARS seul n'a pas marche ; cg_draw2D est bien un dvar du runtime, table a
  95164659 dans iw4l-re4.exe). A verifier ; sinon essayer IW4L_CONSOLE_STDIN (console via
  stdin, string dans l'exe).
- Diagnostic zombies invisibles : BO1Z.ini (a cote de bio4.exe) accepte [BO1Z] FreezeCap=0
  (coupe le gel max-alive) et IntroFreeze=0 (coupe le gel d'intro 3.5 s). Test : runtime ACTIF
  avec FreezeCap=0 -> si zombies visibles, le bit be_flag 4 est en cause ; sinon le probleme
  est dans le chemin de rendu du runtime (culling camera ?). NB : le gel actif aussi sans
  runtime, ou les zombies etaient visibles -> suspicion culling/camera du runtime.

**A faire (prochain)** :
- Calibrer splat score + ammo box BO1 + dpad art sur capture BO1 de reference.
- Voiclines contextuelles (degat recu, achat perk, low ammo...) - categories plr0 connues.
- Effets QuickRevive/DoubleTap cote hote ; Pack-a-Punch ; vagues reelles.
- Optimisation qualite/fluidite runtime (hors 30 fps).

## 17/11 - Sting perk + crosshair/D-pad retires + round change mp3 + diagnostic

**Retour test (21h25)** : 2 captures (exemple bo1.png = reference BO1, screen in-game).
- Perks : pas d'animation de bouteille, le gulp etait le mauvais bruit, le jingle etait
  le jingle entier au lieu du "refrain" qu'on entend en buvant, le jingle de manche absent
  (fichier "Call of Duty Zombies - Round Change Sound Effect.mp3" fourni).
- Zombies toujours invisibles parfois ; degats intermittents (parfois ne tuent pas).
- A retirer : crosshair fictif du milieu + lettres "UP/DN/LT/RT" a gauche.

**Fait** :
- Perk : achat = sting du perk (bo1_sting_juggernog/doubletap/quickrevive) joue immediatement,
  sur canal MCI (survit aux quips). Plus de gulp (c'etait le mauvais bruit) ; le full jingle ne
  joue plus a l'achat (reserve a la machine). Quick Revive : sting extrait de la fin du jingle
  (2.5 s). Speed Cola : pas de sting dans les .ff -> silencieux.
- Round : bo1_round_change.mp3 (fichier fourni) joue a la fin de manche (MCI).
- Crosshair central retire ; bloc D-pad "UP/DN/LT/RT" retire.
- PROJET-NOTES + BO1Z.ini (FreezeCap/IntroFreeze) toujours en place pour le test invisibilite.

**Non fait (a venir)** :
- Animation de bouteille (viewmodel RE4) : necessite reverse-engineering de l'anim joueur
  (rno0/rno1/rno2) + modele de bouteille -> gros chantier, reporte.
- Degats intermittents : envoyer un extrait de MW2RE4.log avec une sequence "shot N ... BulletHit
  returned 1" sans ligne damage qui suit, pour voir si c'est host ou cible.
- Invisibilite zombies : test avec BO1Z.ini (FreezeCap=0) requis pour trancher.
- Calibration HUD exacte (splat, ammo, dpad) : les coords actuelles sont estimees ; une capture
  BO1 (deja fournie) permettra d'ajuster les positions au pixel.

## 18/11 - Reprise technique (non compilee / non testee en jeu)

- `src/re4z/zombies.cpp` : correction de la mise a l'echelle des PV. L'ancien scan rehaussait
  les PV au niveau cible a chaque frame des que l'ennemi tombait sous ce seuil, ce qui pouvait
  le rendre pratiquement immortel. Le nouvel ajustement s'applique une seule fois aux ennemis
  nouvellement observes (adresses suivies et remises a zero au changement de salle).
- `ScanEnemies()` verifie maintenant que `GetGame()` reste valide avant de lire le room id.
- Le gel `FreezeCap` et `IntroFreeze` est desormais desactive par defaut, car il modifie le bit
  mouvement des ennemis et peut contribuer a l'invisibilite. Les options restent activables dans
  `BO1Z.ini` pour diagnostic.
- Transition de manche : l'ancien test attendait que le compteur de kills remplisse le quota de
  la manche affichee, alors que `RoundForKills()` passe deja a la manche suivante au seuil. La
  fin de manche est maintenant detectee au changement de numero, le fichier fourni
  `bo1_round_change.mp3` est joue et le sting de debut est retarde de 10 s.
- Le record `atoutanimexemple.mp4` confirme une animation 3D de bras/bouteille face a la machine.
  Pas de faux sprite ajoute : l'animation authentique demande un viewmodel/rig/clip jouable dans
  le runtime; le simple overlay D3D9 ne peut pas reproduire correctement ces mouvements.
- L'utilisateur demande maintenant egalement l'import complet des machines Perk-a-Cola et
  Pack-a-Punch BO1 dans RE4 avec interaction. Les assets d'armes T5 deja exportes ne prouvent
  pas la disponibilite des modeles de machines. Il faut d'abord identifier/exporter les XModel,
  materiaux/textures et animations depuis BO1, puis verifier le chemin d'integration pour les
  afficher dans le monde RE4 et implementer une interaction/achat. Pack-a-Punch complet non fait.
- Sauvegarde complete lancee sur `F:\re4z_backup_20261117` apres saturation de E:. Le dossier
  contient au moins HANDOVER.md, la source avant modification et l'enregistrement; comparaison
  HANDOVER identique, source zombies differente comme attendu. Verification checksum complete
  non realisee.
- Correctif runtime `guest.ready=0` (23/11) : les logs demontrent que le hook de redirection BO1Z transforme la carte IW4 demandee en `zombie_cosmodrome`; son GSC T5 appelle `exitLevel` avant publication de `guest.ready`. `MW2RE4_new.cpp` marque maintenant le `CreateProcessW` du runtime par `IW4L_RE4_NATIVE_MAP`; `mapredirect.cpp` laisse la carte IW4 native intacte pendant cette creation. Compilation MinGW i386 reussie pour l'hote et BO1Z; les deux PE32 et exports requis ont ete controles. Deploiement verifie par SHA-256 identiques aux candidats. Aucun lancement du jeu/runtime effectue ici : le test en jeu reste a faire par l'utilisateur.

## 17/11 - HUD exact (calibration sur capture BO1)

**Analyse de la capture BO1 (exemple bo1.png, 1800x917) par masques de couleur** :
- Perk badges : centre du badge possede a (193, 768) -> pitch 1er badge x=53 ref (deja bon),
  Y passe de 289 a 332 ref ( badges SOUS le numero de manche, comme BO1).
- Numero de manche (craie rouge) : (139, 670) -> ref (51.2, 289) : Y passe de 380 a 289
  (au-DESSUS des badges, plus de chevauchement).
- Score : texte a (1327..1397, 769..793) -> ref right edge ~547, baseline ~332 (etait 603/290,
  trop a droite et trop haut). Splat scorebar_zom_1 repositionne en 8:1 derriere le score
  (x 327..551, y 318..346), teinte ARGB(235,105,20,14).
- Popup +N : recale sur le score (537, 332).
- Munitions : deja en bas a droite, conserve.
- Crosshair central et bloc "UP/DN/LT/RT" retires.

**Reste a calibrer** : munitions exactes (les chiffres de la capture BO1 n'ont pas ete isoles
automatiquement - resolution/aliasing). A ajuster au retour visuel.

**Tests demandes** :
1. HUD : score sur splat rouge, numero de manche au-dessus des badges, pas de crosshair, pas de
   lettres UP/DN/LT/RT, pop badge perk toujours OK.
2. Invisibilite zombies : BO1Z.ini avec FreezeCap=0 et IntroFreeze=0, runtime ACTIF.
3. Degats intermittents : extrait MW2RE4.log avec un "shot N ... BulletHit returned 1" sans
   ligne "damage:" qui suit.

## 23/11 - Sequencement animation perk / Pack-a-Punch et diagnostic des banques

- `zombies.cpp` : le retour a l'arme BO1 tenue apres une bouteille est differe de 3 s, au-dela
  du `firstRaiseTime=2.8` lu dans les definitions extraites. Le retour est annule si la ligne
  d'arme tenue a change. Pack-a-Punch attend aussi 3 s avant de demander l'arme amelioree
  (l'ancien delai de 1.5 s coupait le premier raise) et ne fait le switch que si l'arme source
  est toujours tenue.
- Build BO1Z : `build_hudonly.bat` compile avec MinGW i386; seuls avertissements existants et
  casts d'exports/enum, aucune erreur. `BO1Z_new.asi` verifie PE32 puis deploye en
  `game/Bin32/BO1Z.asi`; SHA-256 identiques (`bc5a0969...b5456fa`). Le jeu et le runtime
  n'ont pas ete lances; le comportement reste a tester par l'utilisateur.
- Diagnostic affine : sur cette installation, `common_mp.ff` et `common_zombie.ff` ont le meme
  SHA-256 (`f812069e...543117e`). Le journal historique `1795458148.log` correspond a une
  session sur `zombie_cosmodrome` (2 289 alias, 0 zone gaps, 1 342 alias localises francais) et
  non a la session recente sur la carte IW4 native. Il signalait alors 76 alias de prefetch sans
  son charge (`swallow`, `knuckle_*`, `weap_raise_plr`, etc.).
- Pour la session recente, `latest.log` indique `mp_subbase` (181 alias), `common_mp` T5 (2 287
  alias repertories, 1 513 charges), 1 882 XAnim T5 de `common_mp` et seulement 8 clips dans le
  catalogue XAnim de la carte. Les animations de bouteille et de Pack-a-Punch existent dans les
  exports `common_zombie` sous `assets/t5xanim`, mais pas dans le catalogue de cette carte.
  N'affirmer ni que les alias de l'ancienne carte Zombie sont ceux de la session courante, ni que
  les sons de tir BO1 ont ete refuses sans une trace specifique de tir/alias.
- Audio demande (source modifiee, test en jeu a faire) : `MW2RE4_new.cpp` appelle le son BO1
  depuis `ResolveShots`, apres validation de l'evenement de tir runtime et avant le `BulletHit`
  RE4. Sept echantillons sont mappes par nom d'arme, avec trois voix MCI par categorie; les
  erreurs d'ouverture/lecture sont journalisees. Le chemin MCI ouvre maintenant correctement
  un WAV dont le chemin peut contenir des espaces. Pas de deduplication artificielle par tick,
  afin de ne pas perdre deux vraies detections de tir consecutives. `zombies.cpp` joue
  `bo1_perk_drink.wav` sur un canal independant lors d'un achat valide.
- Verification/deploiement : compilation syntaxique MinGW i386 reussie, puis build complet de
  `MW2RE4_new.asi` reussi avec `-lwinmm` (necessaire pour MCI). Le candidat est PE i386 et
  exporte `MW2RE4_GetHits`, `MW2RE4_SetPerks`, `MW2RE4_PlayPerkAnim` et
  `MW2RE4_GetLastEquippedBo1Row`. Deploye dans `game/Bin32/MW2RE4.asi`; SHA-256 identique au
  candidat : `6888f5beb88fd6548b517bcced7afff90fd9d622a136d68e7a3316a4903ef0c9`.
  Aucun lancement du runtime/jeu : le correctif est compile et deploye mais son comportement
  audio n'est pas encore valide en jeu. Demander un essai utilisateur sur quelques armes et un
  achat de perk, puis examiner `MW2RE4.log` pour `BO1 audio: opened/cannot open/cannot play`.
  `git diff --check` est propre (avertissements CRLF seulement).
- Essai Cosmodrome demande pour comparer les sons : suppression du fallback force vers
  `mp_subbase` dans `StartRuntime()` et configuration installee `Skin=5` (`BO1 Zombies
  (Cosmodrome)`). Build MinGW i386 reussi (avertissement existant sur la declaration
  `extern MW2RE4_LifeDownOrigPtr`), ASI PE32 redeploye; SHA-256 source/deploye identique
  `5576ed73ed257369965d202d8eb3f52bb1aa3f2ded26a80fbd832da29c9f631b`. Ni jeu ni runtime
  lances. Ancien log prouve que le runtime charge `mp_subbase` avec `Skin=6`; Cosmodrome
  a deja provoque `common_scripts/_dynamic_world:120:3 ... foreach requires an array` puis
  `exitLevel`, donc son chargement jusqu'au gameplay/audio reste a confirmer par l'utilisateur.
  Diagnostic audio separe : `Bo1ShotSound()` filtre les armes T5 `1195-1268`; les armes
  MW2 classiques ne sont pas couvertes par ce mapping. Le code des sons n'a pas ete modifie
  pendant cet essai de carte.
- Retour utilisateur / journaux Cosmodrome : `latest.log` confirme map T5 demandee puis chargee
  (50.2 s), avant erreur `common_scripts/_dynamic_world:120:3 in player_init: foreach requires
  an array`, `exitLevel`, session intermission et fin du runtime (process_exit code 0). Le host
  reste alors `guest ready=0` et attend jusqu'au timeout : le chargement infini est bien la
  consequence de la fin de session GSC, non un chargement encore en cours.
- Apres le retour utilisateur, tentative orientee besoin : garder la map chargee
  `zombie_cosmodrome` (Skin=5) et, apres `wait world`, executer `set mapname mp_subbase`
  juste avant `spawn 0`, pour que `common_scripts/utility::isSP()` (qui renvoie vrai quand le
  nom de map ne commence pas par `mp_`) fasse prendre au `_dynamic_world::player_init()` le
  chemin MP sans iteration sur `level.players`. Cela conserve la zone T5 selectionnee mais est
  experimental : pas teste, le moteur peut ne pas laisser changer ce dvar ou d'autres natifs
  GSC T5 peuvent manquer. Build MinGW i386 OK (avertissement `extern MW2RE4_LifeDownOrigPtr`),
  PE32 redeploye; SHA-256 source/deploye `e8362f8ddf3e339f3f20ca7bcdd6d108057b24d04c9d39f9f9ba1d7c55aeb176`.
  Re4/runtime pas lances. Redemarrer RE4 et examiner latest.log apres cet essai, surtout une
  eventuelle nouvelle `gsc: runtime_error`; ne pas conclure que Cosmodrome est corrige avant.

## 24/11/2026 — Checkpoint : separation des taches + assets BO1

### Repartition du travail
- **Autre IA** : runtime MW2, interface/HUD BO1 fidele, fluidite.
- **Ici** : tout ce qui concerne les assets BO1 et le monde 3D (hors runtime).

### Runtime (cote autre IA, puis replacement de map)
- L'autre IA a fait remarcher le runtime en chargeant la map T5 selectionnee puis
  en spoofant `set mapname mp_subbase` apres `wait world`, avant `spawn 0` et
  `force_match_start`. Le GSC IW4 (`common_scripts/_dynamic_world`) prend ainsi
  son chemin multiplayer au lieu d'iterer `level.players` (absent). La zone T5
  rechargee reste celle du skin, d'ou le retour des sons zombies BO1.
- **Decision utilisateur** : remplacer `zombie_cosmodrome` par
  `zombie_cod5_prototype` (Nacht der Untoten). Cosmodrome deja observe en timeout.
  `zombie_cod5_prototype.ff` = 32,8 Mo contre 50,6 Mo ; les deux chargent
  `common_zombie.ff` (48,9 Mo) qui contient armes/sons ZM.
- Modifie ici dans `MW2RE4_new.cpp` : `kSkins[5]` = `"BO1 Zombies (Nacht der
  Untoten)"` / `zombie_cod5_prototype` ; `Skin=5` dans `MW2RE4.ini` pointe dessus.
  Commentaires du spoof mis a jour. `mapredirect.cpp` : fallback IW4 -> T5
  redirige maintenant vers `zombie_cod5_prototype` et plus vers cosmodrome.
- **Build host** : `-lwinmm` est desormais OBLIGATOIRE (l'autre IA a ajoute
  `mciGetErrorStringA`) :
  `g++ -m32 -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ -o MW2RE4_new.asi MW2RE4_new.cpp -lgdi32 -luser32 -lwinmm`.
  Verifie : aucune chaine `zombie_cosmodrome` dans les deux ASI deployes.

### Assets BO1 importes ici (deploys dans `game\Bin32\re4z\`)
- **Vraie machine Pack-a-Punch** : `zombie_vending_packapunch` (COSMODROME.ff)
  -> `packapunch.r4m`. Remplace le placeholder Juggernog dans `worldmachines.cpp`.
- **Couvercle de la boite** : `zombie_treasure_box_lid` -> `boxlid.r4m`.
- **14 armes viewmodel BO1** -> `boxgun_*.r4m` : 1911, MP5, AK74u, M14, Ithaca,
  Python, **Ray Gun**, Thundergun, PM63, SPAS, HK21, L96, Dragunov, AK47.
  Lignes d'equipement prises dans `bo1_catalog.h` ; Thundergun/AK47 n'ont pas de
  ligne ZM exacte -> substitutions 1242 / 1196.
- **Vrais dessins a la craie** : 12 `chalk_weapon_*` extraits des .ff BO1,
  convertis en `assets\hud\chalk\*.dds` (fond noir rendu transparent via alpha).
  Les murales utilisent mp40/m14/ithaca au lieu des icons HUD. Script :
  `tools\chalk_png_to_dds.py`.
- **Vraie jingle de la boite** : melodie Zombies reelle (7,4 s) ->
  `bo1_mysterybox_jingle.wav` ; arret a la recuperation de l'arme. La source
  `music_box_00.opus` n'est pas indexee par GrayWolf ni presente dans les IWD.

### Logique boite / monde
- Cycle BO1 : couvercle s'ouvre, armes 3D defilent 2,5 s, arme choisie reste
  flotter, **F pour la prendre** (`g_boxWaiting`). Ancien sprite 2D retire du HUD.
- **Correction d'orientation** : tous les modeles exportes sont Z-up.
  `mysterybox.r4m` etait declare `zUp=false` -> boite couchee sur le cote.
  Passe en `zUp=true` : ouverture vers le haut, `??` du couvercle face haut.
  Couvercle et armes convertis Z-up dans `WorldModel_DrawHinged/DrawCentered`.
- `worldmodel.cpp` : nouveaux `WorldModel_DrawMatrix`, `DrawCentered`, `DrawHinged`.

### En attente / non resolu
- **Derive des modeles** (toujours rapporte par l'utilisateur) : les modeles
  glissent legerement quand la cam bouge. Pas de correction appliquee sans
  mesure. Ajoute dans `worldmodel.cpp` :
  - touche **F9** = bascule la correction d'aspect GX (defaut Hor+ boshjerns vs
    projection GX brute) ;
  - log one-shot dans `RE4Z.log` : `cam GX:` (view/proj derives) et `cam D3D:`
    (GetTransform reels). A comparer apres un lancement.
- Tests utilisateur a faire : runtime sur Nacht der Untoten (sons d'armes ZM),
  orientation boite + 14 armes, F9 et les deux lignes `cam ...`.

### Binaires deployes
- `BO1Z.asi` 806 279 o, `MW2RE4.asi` deploye le 24/11 (build avec `-lwinmm`).
- Ni jeu ni runtime lances ici : tous les comportements restent a valider en jeu.
