#include "zombies.h"
#include "game.h"
#include "log.h"
#include "worldmachines.h"

#include <cstring>
#include <mmsystem.h>
#include <string>
#pragma comment(lib, "winmm.lib")

namespace {

static bool BuildSoundPath(char* out, size_t outSize, const char* filename) {    char exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    char* slash = strrchr(exePath, '\\');
    if (!slash) return false;
    *slash = '\0';
    snprintf(out, outSize, "%s\\re4z\\assets\\sounds\\%s", exePath, filename);
    return true;
}
static void PlayGameSound(const char* filename) {
    char path[MAX_PATH] = {};
    if (BuildSoundPath(path, sizeof(path), filename)) {
        PlaySoundA(path, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    }
}

// PlaySoundA has a single channel: any later call cancels the sound before it,
// which cut the perk jingles off (a gulp right after the jingle, a kill quip
// over the round jingle). The long sounds go through MCI instead: every alias
// is its own channel, so a jingle, a gulp and a quip can all sound at once.
static void MciPlay(const char* alias, const char* filename) {
    char path[MAX_PATH] = {};
    if (!BuildSoundPath(path, sizeof(path), filename)) return;
    const std::string a(alias);
    mciSendStringA(("close " + a).c_str(), nullptr, 0, nullptr);
    mciSendStringA(("open \"" + std::string(path) + "\" alias " + a).c_str(), nullptr, 0, nullptr);
    mciSendStringA(("play " + a).c_str(), nullptr, 0, nullptr);
}

// Tank Dempsey voice line helper: plays dempsey_<cat>_<n>.wav on the "vox"
// channel, rotating through the available variants. maxN = number of files
// exported for that category (0 = no-op).
static void DempseyVox(const char* cat, int maxN, int& idx) {
    if (maxN <= 0) return;
    char f[64];
    snprintf(f, sizeof(f), "dempsey_%s_%d.wav", cat, idx % maxN);
    idx = (idx + 1) % maxN;
    MciPlay("vox", f);
    Log("vox: %s", f);
}

// Dropped power-ups (spawn on kill, walk over to collect).
struct DroppedPowerup {
    bool active = false;
    int type = 0;
    float x = 0, y = 0, z = 0;
    DWORD born = 0;
};
static DroppedPowerup g_drops[8];
static DWORD g_powerupEnd[5] = {};
static const char* kPowerupVox[5] = {
    "insta", "nuke", "ammo", "double", "firesale"
};

static int PowerupSpawn(float x, float y, float z) {
    // Keep the drop near the player so it is actually visible: a zombie can die
    // far outside the camera view, and a drop there would never be seen.
    Vec3 pp{};
    if (ReadPlayerPos(&pp)) {
        float dx = x - pp.x, dz = z - pp.z;
        const float dist2 = dx * dx + dz * dz;
        if (dist2 > 900.0f * 900.0f) {
            const float inv = 900.0f / sqrtf(dist2);
            x = pp.x + dx * inv;
            z = pp.z + dz * inv;
        }
    }
    for (DroppedPowerup& d : g_drops) {
        if (d.active) continue;
        d.active = true;
        d.type = rand() % 5;
        d.x = x + ((float)(rand() % 400) - 200.0f);
        d.y = y;
        d.z = z + ((float)(rand() % 400) - 200.0f);
        d.born = GetTickCount();
        MciPlay("pwspawn", "bo1_mysterybox_move.wav");
        MciPlay("halo", "pwhalo.wav");
        Log("powerup: spawned type %d at (%.0f %.0f)", d.type, d.x, d.z);
        return d.type;
    }
    return -1;
}

static void PowerupCollect(int type) {
    static int voxIdx[5] = {};
    g_powerupEnd[type] = GetTickCount() + 30000;
    char vox[64];
    snprintf(vox, sizeof(vox), "powerup_%s", kPowerupVox[type]);
    int n = (type == 3 || type == 4) ? 3 : ((type == 1) ? 4 : 3);
    DempseyVox(vox, n, voxIdx[type]);
    MciPlay("pwgrab", "pwpickup.wav");
    static const char* kAnn[5] = {
        "ann_ann_instakill.wav", "ann_ann_nuke.wav", "ann_ann_maxammo.wav",
        "ann_ann_doublepoints.wav", "ann_firesale_short.wav"
    };
    if (type >= 0 && type < 5) MciPlay("ann", kAnn[type]);
    Log("powerup: collected type %d, active 30s", type);
}

} // namespace

ZombiesHud g_hud;

// ---------------------------------------------------------------------------
// BO1 Zombies round table (Ascension / zombie_temple).
//
// Taken verbatim from the shipped gametype scripts:
//   zone/Common/common_zombie.ff      -> maps/_zombiemode.gsc
//   zone/Common/zombie_temple.ff      -> maps/zombie_temple_spawning.gsc
//
//   default_max_zombie_func(max_num)      (_zombiemode.gsc:2482)
//   ai_calculate_health(round)             (_zombiemode.gsc:3422)
//   round_think()                          (_zombiemode.gsc:3304)
//
// Base variables (_zombiemode.gsc:546-554):
//   zombie_max_ai = 24, zombie_ai_per_player = 6,
//   zombie_health_start = 150, zombie_health_increase = 100,
//   zombie_health_increase_multiplier = 0.1,
//   zombie_spawn_delay = 2.0 s, zombie_between_round_time = 10 s,
//   zombie_move_speed_multiplier = 8
// ---------------------------------------------------------------------------

constexpr int kZombieMaxAi = 24;
constexpr int kZombieAiPerPlayer = 6;
constexpr int kZombieHealthStart = 150;
constexpr int kZombieHealthIncrease = 100;
constexpr float kZombieHealthIncreaseMultiplier = 0.1f;
constexpr float kZombieSpawnDelay = 2.0f;      // seconds
constexpr int kZombieBetweenRoundTime = 10;    // seconds
constexpr float kZombieMoveSpeedMultiplier = 8.0f;

// RE4 memory layout (bio4.exe 1.1.0), validated by MW2RE4 / re4_tweaks.
// See F:\Desktop\Divers\Deep\MW2-RE4-main\host\src\Re4Game.h
namespace {
constexpr size_t kUnitBeFlag = 0x04;   // cUnit::be_flag
constexpr size_t kEmHp = 0x324;        // cEm::hp, int16
constexpr size_t kMgrArray = 0x04;     // cManager<T>
constexpr size_t kMgrCount = 0x08;
constexpr size_t kMgrStride = 0x0C;
// cItem (0xE bytes)
constexpr size_t kItemSize = 0x0E;
constexpr size_t kItemId = 0x00;
constexpr size_t kItemNum = 0x02;
constexpr size_t kItemFlag = 0x04;     // bit 0 = in use
constexpr size_t kItemChr = 0x05;
constexpr size_t kItemAmmo = 0x08;     // loaded ammo, stored << 3
constexpr size_t kItemMgrWep = 0x0C;
constexpr size_t kItemMgrItems = 0x14;
constexpr size_t kItemMgrCount = 0x1C;
} // namespace

// BO1 solo scoring (_zombiemode.gsc:575, :585).
constexpr int kZombieScoreKill1Player = 50;
constexpr int kZombieScoreDamageNormal = 10;

// RE4 stage enemies start at 1000 hp (em29.cpp:361, em3a.cpp:1510). Anything at
// or below that value is considered "untouched" and can be scaled to the round
// health.
constexpr int kRe4EnemyDefaultHp = 1000;

// Total number of zombies to kill during a round (BO1 "zombie_total").
// default_max_zombie_func() scales the AI budget by the round number.
int Zombies_RoundSize(int round) {
    if (round < 1) round = 1;

    // max = zombie_max_ai + per-player bonus * multiplier
    int multiplier = round / 5;
    if (multiplier < 1) multiplier = 1;
    // After round 10 the AI count grows exponentially.
    if (round >= 10) {
        multiplier = (int)(multiplier * round * 0.15f);
    }
    // Solo player: half the per-player bonus.
    int max = kZombieMaxAi + (int)(0.5f * kZombieAiPerPlayer * multiplier);

    // default_max_zombie_func()
    if (round == 1) return (int)(max * 0.25f);
    if (round < 3) return (int)(max * 0.3f);
    if (round < 4) return (int)(max * 0.5f);
    if (round < 5) return (int)(max * 0.7f);
    if (round < 6) return (int)(max * 0.9f);
    return max;
}

// Cumulative kill threshold at which a round starts (BO1 kill counter).
// Round 1 starts at 0 kills, round N+1 at the sum of the sizes of rounds 1..N.
int Zombies_RoundStartKills(int round) {
    if (round <= 1) return 0;
    int total = 0;
    for (int r = 1; r < round; ++r) total += Zombies_RoundSize(r);
    return total;
}

// Zombie health for a round (ai_calculate_health).
int Zombies_ZombieHealth(int round) {
    int hp = kZombieHealthStart;
    for (int i = 2; i <= round; ++i) {
        if (i >= 10) {
            hp += (int)(hp * kZombieHealthIncreaseMultiplier);
        } else {
            hp += kZombieHealthIncrease;
        }
    }
    return hp;
}

// BO1 spawn delay shrinks by 5% after every round (round_think()).
float Zombies_SpawnDelay(int round) {
    float delay = kZombieSpawnDelay;
    for (int i = 1; i < round; ++i) {
        if (delay > 0.08f) delay *= 0.95f;
    }
    return delay < 0.08f ? 0.08f : delay;
}

// Zombie move speed for a round (round_think()).
float Zombies_MoveSpeed(int round) {
    return round * kZombieMoveSpeedMultiplier;
}

// BO1-style max alive zombies and spawn interval for the HUD readout.
int Zombies_MaxAlive(int round) {
    if (round < 1) round = 1;
    // BO1 caps concurrent enemies at 31 (zombie_temple_spawning.gsc:269).
    int max = kZombieMaxAi + (int)(0.5f * kZombieAiPerPlayer * (round / 5 < 1 ? 1 : round / 5));
    if (max > 31) max = 31;
    if (max < 4) max = 4;
    return max;
}

int Zombies_SpawnInterval(int round) {
    return (int)(Zombies_SpawnDelay(round) * 1000.0f);
}

namespace {

int g_lastKills = 0;
int g_lastHits = 0;
int g_kills = 0;      // kills counted by the ASI (RE4 has no global counter)
int g_hits = 0;       // damage events counted by the ASI
int g_lastRound = 0;bool g_hideNativeHud = true;
bool g_forceInGame = false;
bool g_spawnPacing = true;   // F7 toggles the concurrent-enemy cap
bool g_hudClearedByUs = false;
DWORD g_hitmarkerUntil = 0;
DWORD g_killmarkerUntil = 0;
DWORD g_bannerUntil = 0;
DWORD g_pointsUntil = 0;
DWORD g_roundCompleteUntil = 0;
DWORD g_intermissionUntil = 0;
DWORD g_popupBorn = 0;
float g_popupDx = 0.0f;
float g_popupDy = 0.0f;
// MW2's damage hook counts every enemy hit (MW2RE4.asi exports MW2RE4_GetHits);
// BO1 awards 10 points per hit, on top of 50 per kill. The host .asi may load
// after ours, so the lookup retries every second until it succeeds.
using GetHitsFn = uint32_t(__cdecl*)();
GetHitsFn g_mw2GetHits = nullptr;
using SetPerksFn = void(__cdecl*)(uint32_t);
SetPerksFn g_mw2SetPerks = nullptr;
DWORD g_nextHitResolve = 0;
uint32_t g_lastHitCount = 0;

// BO1 Perk-a-Cola. Slot order matches overlay.cpp's g_perkNames; costs are the
// BO1 solo prices. MW2RE4_SetPerks receives the owned bitmask so the host can
// apply the effects (Juggernog halves incoming damage, Speed Cola halves the
// MW2 reload time). Quick Revive and Double Tap have no effect yet.
struct PerkInfo { const char* name; int cost; uint32_t bit; const char* sting; };
const PerkInfo kPerks[4] = {
    { "JUGGERNOG", 2500, 1, "bo1_sting_juggernog.wav" },
    { "QUICK REVIVE", 500, 2, "bo1_sting_quickrevive.wav" },
    { "SPEED COLA", 3000, 4, nullptr },
    { "DOUBLE TAP", 2000, 8, "bo1_sting_doubletap.wav" },
};
bool g_perkOwned[4] = {};
DWORD g_perkBoughtAt[4] = {};
int g_pointsSpent = 0;
bool g_perkMenu = false;
// Mystery Box: lid opens, 3D weapons spin, then the chosen gun hovers until F.
bool g_boxSpinning = false;
DWORD g_boxSpinStart = 0;
bool g_boxWaiting = false;   // chosen weapon hovering: press F to take it
DWORD g_perkVoxAt = 0;          // deferred perk voice line (after drink + burp)
const char* g_perkVoxCat = nullptr;
int g_boxResult = 0;  // index into the box weapon list
// Pack-a-Punch: 5000 points, upgrades the equipped weapon to its _upgraded row.
bool g_packaPunched = false;
int g_packaWeaponId = 0;
// Deferred equip of the upgraded weapon, sent after the knuckle-crack animation
// (the runtime rejects a second request while the first is still pending).
uint32_t g_packaPendingRow = 0;
DWORD g_packaEquipAt = 0;
using EquipRowFn = void(__cdecl*)(uint32_t);
static EquipRowFn g_mw2EquipRow = nullptr;
using PlayPerkAnimFn = void(__cdecl*)(int);
static PlayPerkAnimFn g_mw2PlayPerkAnim = nullptr;
static PlayPerkAnimFn ResolvePlayPerkAnim() {
    if (HMODULE mw2 = GetModuleHandleA("MW2RE4.asi"))
        return (PlayPerkAnimFn)GetProcAddress(mw2, "MW2RE4_PlayPerkAnim");
    return nullptr;
}
static EquipRowFn ResolveEquipRow() {
    if (HMODULE mw2 = GetModuleHandleA("MW2RE4.asi"))
        return (EquipRowFn)GetProcAddress(mw2, "MW2RE4_EquipRow");
    return nullptr;
}
using GetLastRowFn = uint32_t(__cdecl*)();
static GetLastRowFn g_mw2GetLastEquippedBo1Row = nullptr;
static GetLastRowFn ResolveGetLastEquippedBo1Row() {
    if (HMODULE mw2 = GetModuleHandleA("MW2RE4.asi"))
        return (GetLastRowFn)GetProcAddress(mw2, "MW2RE4_GetLastEquippedBo1Row");
    return nullptr;
}
// The perk jingle is scheduled 0.8 s after the gulp sound (BO1 drinks, then the
// jingle starts), and MCI lets the two overlap without cutting each other off.
int g_pendingJingle = -1;
DWORD g_pendingJingleAt = 0;
// The round start sting is delayed to the end of the between-round time: the
// round counter flips as soon as the quota is reached, but BO1 keeps playing
// the round-over sting for the whole 10 s before the next round roars in.
int g_pendingRoundStart = 0;
DWORD g_pendingRoundStartAt = 0;
// HP-scale bookkeeping: track enemies already given the round's HP so each is
// scaled exactly once (refilling wounded enemies each frame made them immortal).
uint16_t g_hpScaledRoom = 0xFFFF;
int g_hpScaledEnemyCount = 0;
uintptr_t g_hpScaledEnemies[512] = {};

// Isolation toggles for the MW2-runtime "zombies invisible except their hat"
// bug: [BO1Z] FreezeCap and [BO1Z] IntroFreeze in the BO1Z.ini next to bio4.exe
// (default 1 = on). FreezeCap=0 releases the max-alive pacing freeze entirely,
// IntroFreeze=0 skips the 3.5 s map-intro hold, so a test can tell whether the
// freeze bits or the render path hide the zombie bodies.
// Freezing RE4 enemies interferes with their render/animation state in the
// MW2 runtime. Keep both experimental freezes opt-in; they remain available
// through BO1Z.ini for diagnosis.
bool g_freezeCapOn = false;
bool g_introFreezeOn = false;
bool g_infinitePoints = false;  // F6 toggle / [BO1Z] InfinitePoints=1 in BO1Z.ini
static void LoadIsolationToggles() {
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (char* slash = strrchr(exe, '\\')) *(slash + 1) = '\0';
    std::string ini = std::string(exe) + "BO1Z.ini";
    g_freezeCapOn = GetPrivateProfileIntA("BO1Z", "FreezeCap", 0, ini.c_str()) != 0;
    g_introFreezeOn = GetPrivateProfileIntA("BO1Z", "IntroFreeze", 0, ini.c_str()) != 0;
    g_infinitePoints = GetPrivateProfileIntA("BO1Z", "InfinitePoints", 0, ini.c_str()) != 0;
}
// BO1-style map intro: black screen over the freshly loaded stage, every zombie
// held in place, then a fade-in. Turned off once the fade completes.
DWORD g_introStart = 0;
bool g_introDone = false;
DWORD g_lastWaypoint = 0;
int g_lastPoints = 0;

bool TimeActive(DWORD until) {
    return until != 0 && (int32_t)(until - GetTickCount()) > 0;
}

void ReadInventory(ZombiesHud* h) {
    // Validated by MW2RE4 on this exact bio4.exe: ItemMgr is at base + 0x870FD4.
    const uintptr_t im = GameBase() + 0x870FD4;
    if (!im || !IsRangeReadable((const void*)im, 0x30)) return;

    const uint8_t* mgr = (const uint8_t*)im;
    const uint8_t* pWep = *(const uint8_t* const*)(mgr + kItemMgrWep);   // equipped weapon item
    h->weaponId = *(const uint16_t*)(mgr + 0x10);                        // cItemMgr::m_wep_id
    h->ammoValid = true;

    if (pWep && IsRangeReadable(pWep, kItemSize)) {
        // cItem::ammo holds the loaded rounds shifted left by 3.
        const uint16_t ammo = *(const uint16_t*)(pWep + kItemAmmo);
        h->ammoLoaded = ammo >> 3;
    }

    // Reserve: sum of the carried ammo items (cItem::chr is the item type;
    // ammunition is type 2 in RE4's item table).
    const uint8_t* pItem = *(const uint8_t* const*)(mgr + kItemMgrItems);
    const int n = *(const int*)(mgr + kItemMgrCount);
    if (!pItem || n <= 0 || n > 512 || !IsRangeReadable(pItem, (size_t)n * kItemSize)) return;

    int reserve = 0;
    for (int i = 0; i < n; ++i) {
        const uint8_t* it = pItem + (size_t)i * kItemSize;
        const uint8_t flags = it[kItemFlag];
        const uint8_t type = it[kItemChr];
        if ((flags & 1) && type == 2) {
            reserve += *(const uint16_t*)(it + kItemNum);  // cItem::num
        }
    }
    h->ammoReserve = reserve;
}

const char* WeaponName(int itemId) {
    switch (itemId) {
    case 0x23: return "M1911";
    case 0x24: return "CZ75";
    case 0x21: return "RED9";
    case 0x22: return "PUNISHER";
    case 0x25: case 0x26: return "M870";
    case 0x27: case 0x28: return "STRIKER";
    case 0x2E: case 0x6B: case 0x99: case 0x2F: case 0x6C: case 0x51:
        return "TMP";
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x3E:
        return "MAGNUM";
    case 0x34: return "ROCKET";
    case 0x35: return "KNIFE";
    case 0x29: return "RIFLE";
    default: return "M1911";
    }
}

// Round for a given cumulative kill count, using the BO1 round table.
int RoundForKills(int kills) {
    if (kills <= 0) return 1;
    int round = 1;
    // Walk forward until the next round's start threshold is passed.
    while (round < 200 && kills >= Zombies_RoundStartKills(round + 1)) {
        ++round;
    }
    return round;
}

// Walks the live enemy list of the EmMgr.
//
// `maxAlive` caps how many enemies may move at the same time, the way BO1
// limits the concurrent zombie count: the surplus is frozen in place
// (cEm::be_flag bit 0x4, "the enemy's move routine is skipped", em_wrap.cpp:469)
// and released again as the others die.
//
// `targetHp` raises the health of untouched enemies to the round's value,
// which is how BO1 scales zombie health per round (ai_calculate_health).
//
// `alivePtr` receives the number of living enemies, `deadPtr` the number of
// enemies that died since the previous call (so the ASI can count kills).
// `liveOut` receives the addresses of the living enemies.
int ScanEnemies(void* player, int maxAlive, int targetHp, int* alivePtr, int* deadPtr,
                uintptr_t* liveOut) {
    const uintptr_t mgr = GameBase() + 0x7FDB04;  // cEmMgr EmMgr
    if (alivePtr) *alivePtr = 0;
    if (deadPtr) *deadPtr = 0;
    if (!mgr || !IsRangeReadable((const void*)mgr, 0x38)) return 0;

    // cManager<cEm>: array 0x04, count 0x08, stride 0x0C.
    uint8_t* pArray = *(uint8_t**)(mgr + kMgrArray);
    uint32_t nArray = *(uint32_t*)(mgr + kMgrCount);
    uint32_t stride = *(uint32_t*)(mgr + kMgrStride);

    if (!pArray || nArray == 0 || nArray > 4096 || stride < 0x400 || stride > 0x2000) {
        return 0;
    }
    if (!IsRangeReadable(pArray, (size_t)nArray * stride)) return 0;

    // Reset per-enemy HP bookkeeping when RE4 changes rooms; manager slots can
    // be recycled between stages.
    const uint8_t* game = (const uint8_t*)GetGame();
    if (!game) return 0;
    const uint16_t room = *(const uint16_t*)(game + 0x4F9C);
    if (room != g_hpScaledRoom) {
        g_hpScaledRoom = room;
        g_hpScaledEnemyCount = 0;
    }

    // Pass 1: collect the living enemies.
    uint8_t* alive[256];
    bool frozen[256];
    int count = 0;
    // The manager keeps a per-unit flag list; cUnit::be_flag bit 0 = alive.
    for (uint32_t i = 0; i < nArray && count < 256; ++i) {
        uint8_t* em = pArray + (size_t)i * stride;
        if (!IsRangeReadable(em, 0x330)) continue;
        const uint32_t beFlag = *(uint32_t*)(em + kUnitBeFlag);
        if ((beFlag & 1) == 0) continue;      // not alive
        if (em == (uint8_t*)player) continue;  // the player is not an enemy
        const int16_t hp = *(int16_t*)(em + kEmHp);
        if (hp <= 0) continue;                // already dead
        alive[count] = em;
        frozen[count] = (beFlag & 4) != 0;
        ++count;
    }

    // Set a newly observed enemy's BO1 round health once. Never top up an
    // already-seen enemy: doing that each frame made wounded Ganados immortal.
    uintptr_t currentHpScaled[512];
    int currentHpScaledCount = 0;
    for (int i = 0; i < count && currentHpScaledCount < 512; ++i) {
        const uintptr_t address = (uintptr_t)alive[i];
        bool alreadySeen = false;
        for (int j = 0; j < g_hpScaledEnemyCount; ++j) {
            if (g_hpScaledEnemies[j] == address) { alreadySeen = true; break; }
        }
        if (!alreadySeen) {
            const int16_t hp = *(int16_t*)(alive[i] + kEmHp);
            // RE4's ordinary Ganados enter with 1000 HP, while later-round
            // zombies may need more than that. Set the initial value exactly
            // once for each newly seen enemy; never refill it on later scans.
            if (targetHp > hp && hp > 0 && hp <= kRe4EnemyDefaultHp && targetHp <= 32767)
                *(int16_t*)(alive[i] + kEmHp) = (int16_t)targetHp;
        }
        currentHpScaled[currentHpScaledCount++] = address;
    }
    memcpy(g_hpScaledEnemies, currentHpScaled, (size_t)currentHpScaledCount * sizeof(uintptr_t));
    g_hpScaledEnemyCount = currentHpScaledCount;

    // Pass 2: spawn pacing. The surplus is frozen, the frozen ones are
    // released as the others die. A negative maxAlive means the BO1 map intro:
    // every zombie is held in place until the fade-in ends.
    if (maxAlive != 0) {
        const int cap = maxAlive < 0 ? 0 : maxAlive;
        int moving = 0;
        for (int i = 0; i < count; ++i) {
            if (frozen[i]) continue;
            ++moving;
        }
        for (int i = 0; i < count; ++i) {
            uint32_t beFlag = *(uint32_t*)(alive[i] + kUnitBeFlag);
            const bool isFrozen = (beFlag & 4) != 0;
            if (!isFrozen) {
                if (moving > cap) {
                    *(uint32_t*)(alive[i] + kUnitBeFlag) = beFlag | 4;  // freeze
                    --moving;
                }
            } else if (moving < cap) {
                *(uint32_t*)(alive[i] + kUnitBeFlag) = beFlag & ~4u;    // release
                ++moving;
            }
        }
    }
    if (alivePtr) *alivePtr = count;
    if (liveOut) {
        for (int i = 0; i < count && i < 512; ++i) liveOut[i] = (uintptr_t)alive[i];
    }
    return count;
}

// Tracks the living enemies by address so the ASI can count kills: RE4 has no
// global kill counter, but an enemy that leaves the live list is a kill.
struct KillTracker {
    uintptr_t seen[512];
    float pos[512][3];
    int count = 0;

    void Reset() { count = 0; }

    // Adds the current live list; returns how many known enemies disappeared.
    // lastDeath receives the world position of the most recent one.
    int Update(const uintptr_t* live, int liveCount, float lastDeath[3]) {
        int dead = 0;
        for (int i = 0; i < count; ++i) {
            bool found = false;
            for (int j = 0; j < liveCount; ++j) {
                if (seen[i] == live[j]) { found = true; break; }
            }
            if (!found) {
                ++dead;
                if (lastDeath) {
                    lastDeath[0] = pos[i][0];
                    lastDeath[1] = pos[i][1];
                    lastDeath[2] = pos[i][2];
                }
            }
        }
        count = liveCount < 512 ? liveCount : 512;
        for (int i = 0; i < count; ++i) {
            seen[i] = live[i];
            const uint8_t* em = (const uint8_t*)live[i];
            if (IsRangeReadable(em, 0xA0)) {
                const float* p = (const float*)(em + 0x94);
                if (std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2])) {
                    pos[i][0] = p[0]; pos[i][1] = p[1]; pos[i][2] = p[2];
                }
            }
        }
        return dead;
    }
};

KillTracker g_killTracker;

} // namespace

const char* Zombies_WeaponName(int itemId) {
    return WeaponName(itemId);
}

void Zombies_PerkInfo(int slot, const char** name, int* cost, bool* owned) {
    if (slot < 0 || slot > 3) return;
    if (name) *name = kPerks[slot].name;
    if (cost) *cost = kPerks[slot].cost;
    if (owned) *owned = g_perkOwned[slot];
}

bool ApplyPerkBuy(ZombiesHud& h, int slot);
void PackaPunch(ZombiesHud& h);

extern "C" __declspec(dllexport) bool __cdecl Zombies_BuyPerk(int slot) {
    return ApplyPerkBuy(g_hud, slot);
}

// Apply a perk purchase to a ZombiesHud (used both by the export and by the
// in-game 1-4 keys). Returns true on success.
bool ApplyPerkBuy(ZombiesHud& h, int slot) {
    if (slot == 4) { // knuckle crack sentinel: trigger Pack-a-Punch
        PackaPunch(h);
        return true;
    }
    if (slot < 0 || slot > 3) return false;
    if (!h.inGame) return false;
    if (g_perkOwned[slot]) return false;
    if (h.points < kPerks[slot].cost) {
        static int nomoneyIdx = 0;
        DempseyVox("nomoney", 3, nomoneyIdx);
        return false;
    }
    g_pointsSpent += kPerks[slot].cost;
    h.points -= kPerks[slot].cost;
    g_perkOwned[slot] = true;
    g_perkBoughtAt[slot] = GetTickCount();
    // The bottle-drink clip is a separate channel so it can overlap the perk sting.
    MciPlay("drink", "bo1_perk_drink.wav");
    if (kPerks[slot].sting)
        MciPlay("sting", kPerks[slot].sting);
    // Dempsey quip on perk purchase (one variant per perk).
    static const char* kPerkVox[4] = { "perk_jugga", "perk_revive", "perk_speed", "perk_doubletap" };
    if (slot >= 0 && slot < 4) {
        // Dempsey comments after the 3 s bottle-drink animation and the gulp,
        // not on the purchase frame.
        g_perkVoxAt = GetTickCount() + 3000;
        g_perkVoxCat = kPerkVox[slot];
    }
    // Play the 3D bottle-drink animation in the runtime (also works for F8 buys).
    if (!g_mw2PlayPerkAnim) g_mw2PlayPerkAnim = ResolvePlayPerkAnim();
    if (g_mw2PlayPerkAnim) g_mw2PlayPerkAnim(slot);
    if (g_mw2SetPerks) {
        uint32_t mask = 0;
        for (int j = 0; j < 4; ++j)
            if (g_perkOwned[j]) mask |= kPerks[j].bit;
        g_mw2SetPerks(mask);
    }
    Log("perk: bought %s for %d (points now %d)", kPerks[slot].name, kPerks[slot].cost, h.points);
    return true;
}

// Pack-a-Punch: 5000 points, upgrades the equipped weapon to its _upgraded BO1 row.
// itemId -> upgraded row mapping (RE4 itemId -> BO1 catalog row of the upgraded weapon).
static int PackaPunchRowForWeaponId(int itemId) {
    switch (itemId) {
        case 0x23: return 1238u; // M1911 -> m1911 upgraded zm
        case 0x24: return 1205u; // CZ75  -> cz75 upgraded zm
        case 0x25:
        case 0x26: return 1262u; // M870  -> spas upgraded zm
        case 0x27:
        case 0x28: return 1262u; // STRIKER -> spas upgraded zm
        case 0x29: return 1234u; // RIFLE -> m14 upgraded zm
        case 0x34: return 1242u; // ROCKET -> m72 law upgraded zm
        default: return 0;
    }
}

void PackaPunch(ZombiesHud& h) {
    if (!h.inGame) return;
    if (h.points < 5000) {
        static int nomoneyPaPIdx = 0;
        DempseyVox("nomoney", 3, nomoneyPaPIdx);
        return;
    }
    if (!g_mw2EquipRow) g_mw2EquipRow = ResolveEquipRow();
    if (!g_mw2GetLastEquippedBo1Row) g_mw2GetLastEquippedBo1Row = ResolveGetLastEquippedBo1Row();
    if (!g_mw2EquipRow || !g_mw2GetLastEquippedBo1Row) return;
    uint32_t equippedRow = g_mw2GetLastEquippedBo1Row();
    uint32_t row = 0;
    if (equippedRow >= 1195 && equippedRow <= 1273) {
        // BO1 weapon equipped via F10: upgraded row is row - 1 (upgraded rows are odd,
        // normal rows are even and just after in the catalog).
        row = equippedRow - 1;
        if ((row % 2) == 0) row = 0; // sanity: upgraded rows are odd
    } else {
        // Fallback: RE4 itemId -> upgraded row mapping.
        row = (uint32_t)PackaPunchRowForWeaponId(h.weaponId);
    }
    if (row <= 0) return;
    h.points -= 5000;
    g_pointsSpent += 5000;
    g_packaPunched = true;
    g_packaWeaponId = h.weaponId;
    // Knuckle-crack animation first (row 1269), then the upgraded weapon 0.7 s
    // later so the runtime's request-ack handshake has time to clear.
    g_mw2EquipRow(1269u);
    g_packaPendingRow = row;
    // Let the knuckle-crack animation finish before equipping the upgraded
    // weapon, otherwise the equip cuts the animation halfway.
    g_packaEquipAt = GetTickCount() + 1500;
    // Authentic BO1 Pack-a-Punch refrain (the sting played on upgrade, not the
    // full jingle that plays from the machine).
    MciPlay("packa", "bo1_packapunch_sting.wav");
    static int g_packaVoxIdx = 0;
    DempseyVox("wpck_upgrade", 6, g_packaVoxIdx);
    Log("packa-punch: equipped row %u -> knuckle crack -> upgraded row %u (points now %d)", equippedRow, row, h.points);
}

void Zombies_Update() {
    static bool togglesLoaded = false;
    if (!togglesLoaded) {
        togglesLoaded = true;
        LoadIsolationToggles();
    }
    static bool prevF7 = false;
    const bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (f7 && !prevF7) {
        g_spawnPacing = !g_spawnPacing;
        Log("spawn pacing: %s", g_spawnPacing ? "ON" : "OFF");
    }
    prevF7 = f7;

    // F6: toggle infinite points (for testing without grinding).
    static bool prevF6 = false;
    const bool f6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
    if (f6 && !prevF6) {
        g_infinitePoints = !g_infinitePoints;
        Log("infinite points: %s", g_infinitePoints ? "ON" : "OFF");
    }
    prevF6 = f6;

    // F8: toggle the Perk-a-Cola buy menu.
    static bool prevB = false;
    const bool b = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (b && !prevB)
        g_perkMenu = !g_perkMenu;
    prevB = b;

    // F9: force HUD display (toggle g_forceInGame).
    static bool prevF9 = false;
    const bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f9 && !prevF9) {
        if (GetPlayer() && IsRangeReadable(GetPlayer(), 0x800) && GetGame() && IsRangeReadable(GetGame(), 0x8350)) {
            g_forceInGame = !g_forceInGame;
            Log("forced HUD: %s", g_forceInGame ? "ON" : "OFF");
        } else {
            Log("forced HUD: ignored (player/game not valid)");
        }
    }
    prevF9 = f9;

    ZombiesHud h;

    void* pG = GetGame();
    if (!pG) {
        g_hud = h;
        g_lastRound = 0;
        return;
    }

    const uint8_t* g = (const uint8_t*)pG;
    h.valid = true;
    h.roomId = *(const uint16_t*)(g + 0x4F9C);

    // Hits from MW2's damage hook: the delta over the previous read is what the
    // BO1 scoring awards 10 points for.
    if (!g_mw2GetHits && GetTickCount() >= g_nextHitResolve) {
        g_nextHitResolve = GetTickCount() + 1000;
        if (HMODULE host = GetModuleHandleA("MW2RE4.asi")) {
            g_mw2GetHits = (GetHitsFn)GetProcAddress(host, "MW2RE4_GetHits");
            g_mw2SetPerks = (SetPerksFn)GetProcAddress(host, "MW2RE4_SetPerks");
        }
    }
    if (g_mw2GetHits) {
        const uint32_t hits = g_mw2GetHits();
        g_hits += (int)(hits - g_lastHitCount);
        g_lastHitCount = hits;
    }
    // The kill counter is owned by the ASI (see g_killTracker below): RE4 has no
    // global kill count. It is refreshed after the enemy scan.
    h.kills = g_kills;
    // BO1 solo scoring (_zombiemode.gsc:575, :585):
    //   zombie_score_kill_1player = 50, zombie_score_damage_normal = 10.
    // Every award is rounded up to a multiple of 5 (round_up_score).
    int earned = g_kills * kZombieScoreKill1Player + g_hits * kZombieScoreDamageNormal;
    if (g_powerupEnd[3] > GetTickCount()) earned *= 2;  // Double Points
    h.points = earned - g_pointsSpent;
    // Infinite points for testing: toggle with F6 (and in BO1Z.ini [BO1Z] InfinitePoints=1).
    if (g_infinitePoints)
        h.points = 999999;
    h.health = *(const uint16_t*)(g + 0x4FA4);
    h.maxHealth = *(const uint16_t*)(g + 0x4FA6);
    h.weaponNo = *(const uint8_t*)(g + 0x4FB0);
    ReadInventory(&h);
    // Dempsey ammo callouts (throttled).
    {
        static DWORD lastAmmoVox = 0;
        static int ammoLowIdx = 0, ammoOutIdx = 0;
        static bool wasOut = false;
        if (h.ammoValid && GetTickCount() - lastAmmoVox > 8000) {
            if (h.ammoLoaded == 0 && !wasOut) {
                lastAmmoVox = GetTickCount();
                wasOut = true;
                DempseyVox("ammo_out", 5, ammoOutIdx);
            } else if (h.ammoLoaded > 0 && h.ammoLoaded <= 5 && h.ammoLoaded < h.ammoReserve / 4 + 1) {
                lastAmmoVox = GetTickCount();
                DempseyVox("ammo_low", 5, ammoLowIdx);
            }
        }
        if (h.ammoLoaded > 0) wasOut = false;
    }

    if (h.maxHealth > 0 && h.maxHealth < 10000 && h.health >= 0 && h.health <= h.maxHealth) {
        h.healthFrac = (float)h.health / (float)h.maxHealth;
    } else {
        h.health = h.maxHealth = 0;
        h.healthFrac = 0.0f;
    }
    // Dempsey "oh shit" when Leon takes a hit (throttled, 3 s); revive lines
    // when he goes down / gets back up.
    {
        static int lastHp = -1;
        static DWORD lastHitVox = 0;
        static int ohshitIdx = 0, reviveDownIdx = 0, reviveUpIdx = 0;
        static bool wasDown = false;
        if (lastHp >= 0 && h.health < lastHp && h.health > 0 &&
            GetTickCount() - lastHitVox > 3000) {
            lastHitVox = GetTickCount();
            DempseyVox("ohshit", 10, ohshitIdx);
        }
        if (h.health == 0 && lastHp > 0 && !wasDown) {
            wasDown = true;
            DempseyVox("revive_down", 3, reviveDownIdx);
        }
        if (h.health > 0 && wasDown) {
            wasDown = false;
            DempseyVox("revive_up", 5, reviveUpIdx);
        }
        if (h.health > 0) lastHp = h.health;
        if (h.health == 0) lastHp = 0;
    }

    // Room 0 is the front-end/attract screen. Mercenaries stages live in
    // 0x0400..0x04FF (r400 = village). Restrict the prototype HUD/viewmodel to
    // those rooms: the small ids seen in the menus (0x0002) are not gameplay.
    const uint32_t stopFlg = *(const uint32_t*)(g + 0x170);
    const bool mercRoom = h.roomId >= 0x0400 && h.roomId < 0x0500;
    // Only force in-game if player is valid (avoid crashes in menus).
    const bool playerValid = GetPlayer() && IsRangeReadable(GetPlayer(), 0x800) && GetGame() && IsRangeReadable(GetGame(), 0x8350);
    if (g_forceInGame && !playerValid) {
        g_forceInGame = false;
        Log("forced HUD/viewmodel: auto-disabled (player/game not valid)");
    }
    h.inGame = (mercRoom || g_forceInGame) && stopFlg != 0xFFFFFFFF;

    // Minimal machine collision + interaction: push Leon out of a small radius
    // around each machine, and let F buy/interact when close.
    if (h.inGame) {
        uint8_t* pl = (uint8_t*)GetPlayer();
        if (pl && IsRangeReadable(pl, 0xA8)) {
            float* feet = (float*)(pl + 0x94);
            const float R = 900.0f;  // ~0.9 m collision radius
            const WorldMachine* machines = WorldMachines();
            float anch[3]; WorldMachines_Anchor(anch);
            if (std::isfinite(feet[0]) && std::isfinite(feet[2])) {
                for (int i = 0; i < WM_COUNT; ++i) {
                    const float mx = anch[0] + machines[i].offX;
                    const float mz = anch[2] + machines[i].offZ;
                    float dx = feet[0] - mx, dz = feet[2] - mz;
                    float d = sqrtf(dx*dx + dz*dz);
                    if (d < R && d > 0.001f) {
                        const float push = (R - d) / d;
                        feet[0] += dx * push;
                        feet[2] += dz * push;
                    }
                }
            }
            // Interaction: F near a machine (radius ~1.8 m).
            static bool prevFKey = false;
            const bool fk = (GetAsyncKeyState('F') & 0x8000) != 0;
            if (fk && !prevFKey) {
                int nearIdx = WorldMachines_Near(feet[0], feet[2], 1800.0f);
                WorldMachines_Interact(nearIdx);
                if (nearIdx >= 0) {
                    switch (WorldMachine_Type(nearIdx)) {
                        case WM_JUGGERNOG:   ApplyPerkBuy(h, 0); break;
                        case WM_QUICKREVIVE: ApplyPerkBuy(h, 1); break;
                        case WM_SPEEDCOLA:   ApplyPerkBuy(h, 2); break;
                        case WM_DOUBLETAP:   ApplyPerkBuy(h, 3); break;
                        case WM_PACKAPUNCH:  PackaPunch(h); break;
                        case WM_MYSTERY_BOX: {
                            if (g_boxWaiting) {
                                // BO1: the weapon sits above the open box until the
                                // player presses the action key to take it.
                                // Registry rows from bo1_catalog.h (T5 common_zombie).
                                static const uint32_t kRows[16] = {
                                    1239, 1248, 1196, 1235, 1230, 1255,
                                    1257, 1309, 1252, 1263, 1225, 1233,
                                    1213, 1196,
                                    1287, 1286
                                };
                                if (!g_mw2EquipRow) g_mw2EquipRow = ResolveEquipRow();
                                if (g_mw2EquipRow && g_boxResult >= 0 && g_boxResult < 16) {
                                    g_mw2EquipRow(kRows[g_boxResult]);
                                    Log("mystery box: took row %u", kRows[g_boxResult]);
                                }
                                g_boxWaiting = false;
                                mciSendStringA("close boxmusic", nullptr, 0, nullptr);
                                MciPlay("boxgrab", "bo1_mysterybox_move.wav");
                                static int g_boxGrabVoxIdx = 0;
                                const char* cat = "wpck_favorite";
                                int cnt = 6;
                                switch (g_boxResult) {
                                    case 6: cat = "wpck_raygun"; cnt = 6; break;
                                    case 7: cat = "wpck_thunder"; cnt = 5; break;  // Thundergun
                                    case 9: cat = "wpck_shotgun"; cnt = 7; break;
                                    case 10: cat = "wpck_mg"; cnt = 9; break;
                                    case 12: cat = "wpck_sniper"; cnt = 6; break;
                                    case 13: cat = "wpck_smg"; cnt = 5; break;
                                    case 14: cat = "wpck_favorite"; cnt = 6; break;  // tesla
                                    case 15: cat = "wpck_favorite_upgrade"; cnt = 6; break;  // tesla upgraded
                                    default: cat = "wpck_favorite"; cnt = 6; break;
                                }
                                DempseyVox(cat, cnt, g_boxGrabVoxIdx);
                            } else if (!g_boxSpinning && h.points < 950) {
                                static int nomoneyBoxIdx = 0;
                                DempseyVox("nomoney", 3, nomoneyBoxIdx);
                            } else if (!g_boxSpinning && h.points >= 950) {
                                h.points -= 950;
                                g_pointsSpent += 950;
                                g_boxSpinning = true;
                                g_boxSpinStart = GetTickCount();
                                g_boxResult = (int)(GetTickCount() % 16);
                                MciPlay("boxmove", "bo1_mysterybox_move.wav");
                                MciPlay("boxmusic", "bo1_mysterybox_jingle.wav");
                                static int g_boxMoveVoxIdx = 0;
                                DempseyVox("box_move", 6, g_boxMoveVoxIdx);
                                Log("mystery box: spin start, result=%d", g_boxResult);
                            }
                            break;
                        }
                    }
                }
            }
            prevFKey = fk;
        }
    }

    // DPF_COCKPIT = bit 15 of pG->Disp_flg. Clearing it hides the native RE4 HUD
    // (life meter, ammo, timer); the game restores it on room changes, so keep it
    // cleared every frame while the BO1 HUD is active. Only undo our own hide,
    // otherwise we would reveal the HUD during intentional cutscene hides.
    // DPF_COCKPIT = 0x00010000 (bit 16) of g+0x58 = "RE4 HUD hidden". The MW2
    // host also sets it. Set it to hide RE4's native HUD (HP bar, health) so
    // only the BO1 HUD we draw remains. (Was wrongly using bit 15 before.)
    LONG* dispFlg = (LONG*)((uint8_t*)pG + 0x58);
    if (h.inGame && g_hideNativeHud) {
        InterlockedOr(dispFlg, (LONG)0x00010000u);
        g_hudClearedByUs = true;
    } else if (g_hudClearedByUs) {
        InterlockedAnd(dispFlg, ~(LONG)0x00010000u);
        g_hudClearedByUs = false;
    }

    if (!h.inGame) {
        g_hud = h;
        g_lastRound = 0;
        g_lastKills = h.kills;
        g_lastHits = g_hits;
        g_hitmarkerUntil = g_killmarkerUntil = g_bannerUntil = g_pointsUntil = 0;
        g_intermissionUntil = 0;
        // Leaving the stage resets the kill counter and its tracker.
        g_kills = 0;
        g_hits = 0;
        g_killTracker.Reset();
        g_introDone = false;
        g_lastPoints = 0;
        g_lastHitCount = g_mw2GetHits ? g_mw2GetHits() : 0;
        // A new run starts with an empty Perk-a-Cola tab.
        for (int i = 0; i < 4; ++i) { g_perkOwned[i] = false; g_perkBoughtAt[i] = 0; }
        g_pointsSpent = 0;
        g_perkMenu = false;
        g_pendingJingle = -1;
        g_pendingRoundStart = 0;
        if (g_mw2SetPerks) g_mw2SetPerks(0);
        return;
    }

    // Buy a perk with 1-4 while the menu is open. The BO1 solo prices, and a
    // perk is permanent for the run once bought.
    static bool prevDigit[4] = {};
    for (int i = 0; i < 4; ++i) {
        const bool d = (GetAsyncKeyState('1' + i) & 0x8000) != 0;
        if (d && !prevDigit[i] && g_perkMenu)
            ApplyPerkBuy(h, i);
        prevDigit[i] = d;
    }

    // P: Pack-a-Punch (5000 points, upgrades the equipped weapon to its _upgraded row).
    static bool prevP = false;
    const bool p = (GetAsyncKeyState('P') & 0x8000) != 0;
    if (p && !prevP && !g_perkMenu)
        PackaPunch(h);
    prevP = p;

    // Deferred Pack-a-Punch weapon equip (after the knuckle-crack animation).
    if (g_packaPendingRow && GetTickCount() >= g_packaEquipAt) {
        if (!g_mw2EquipRow) g_mw2EquipRow = ResolveEquipRow();
        if (g_mw2EquipRow) g_mw2EquipRow(g_packaPendingRow);
        Log("packa-punch: equipped upgraded row %u", g_packaPendingRow);
        g_packaPendingRow = 0;
    }

    // Deferred perk voice line: fires at the end of the drink animation/gulp.
    if (g_perkVoxCat && g_perkVoxAt && GetTickCount() >= g_perkVoxAt) {
        static int g_perkVoxIdx = 0;
        DempseyVox(g_perkVoxCat, 1, g_perkVoxIdx);
        g_perkVoxCat = nullptr;
        g_perkVoxAt = 0;
    }

    // Mystery Box: after ~2.5 s of 3D weapons spinning, the chosen gun hovers
    // above the open box until the player presses F.
    if (g_boxSpinning && GetTickCount() - g_boxSpinStart >= 2500) {
        g_boxSpinning = false;
        g_boxWaiting = true;
        Log("mystery box: spin done, result=%d (press F)", g_boxResult);
    }

    h.round = RoundForKills(h.kills);
    h.maxAlive = Zombies_MaxAlive(h.round);

    // BO1-style map intro: the first valid frame inside a mercenaries stage
    // starts a 3.5 s hold - 2.2 s of full black, then a 1.3 s fade-in - while
    // every zombie is frozen. Waypoint logging (2 s) records Leon's position so
    // the spawn point can later be calibrated to the village centre.
    const bool mercRoomNow = h.roomId >= 0x0400 && h.roomId < 0x0500;
    const DWORD nowMs = GetTickCount();
    if (h.inGame && mercRoomNow && !g_introDone) {
        g_introDone = true;
        g_introStart = nowMs;
        g_lastPoints = h.points;
        static int levelStartVoxIdx = 0;
        DempseyVox("level_start", 5, levelStartVoxIdx);
        Log("intro: BO1 map intro started (room %04X), enemies held 3.5 s", h.roomId);
    }
    const bool introHolding = g_introFreezeOn && g_introDone && (int32_t)(nowMs - g_introStart) < 3500;
    if (introHolding)
        h.introActive = true;
    if (h.inGame && mercRoomNow && nowMs - g_lastWaypoint >= 2000) {
        g_lastWaypoint = nowMs;
        if (const uint8_t* pl = (const uint8_t*)GetPlayer()) {
            const float* leon = (const float*)(pl + 0x94);  // cCoord::pos, feet
            Log("waypoint: room=%04X Leon=(%.0f, %.0f, %.0f)", h.roomId, leon[0], leon[1], leon[2]);
        }
    }
    if (g_introFreezeOn && g_introDone) {
        const DWORD since = nowMs - g_introStart;
        if (since < 2200) h.introBlack = 1.0f;
        else if (since < 3500) h.introBlack = 1.0f - (float)(since - 2200) / 1300.0f;
    }

    int aliveEnemies = 0;
    uintptr_t live[512];
    ScanEnemies(GetPlayer(), introHolding ? -1 : (g_spawnPacing && g_freezeCapOn ? h.maxAlive : 0),
                Zombies_ZombieHealth(h.round), &aliveEnemies, nullptr, live);
    h.alive = aliveEnemies;

    // Kill counter: RE4 has no global kill count, so the ASI tracks the live
    // enemy list and counts the enemies that disappear from it.
    float deathPos[3] = {};
    const int deadNow = g_killTracker.Update(live, aliveEnemies, deathPos);
    static bool g_powerupSpawnedThisKill = false;
    g_powerupSpawnedThisKill = false;
    if (deadNow > 0) {
        g_kills += deadNow;
        // Power-up drops at the zombie's death spot (100% for testing).
        g_powerupSpawnedThisKill =
            PowerupSpawn(deathPos[0], deathPos[1], deathPos[2]) >= 0;
        // Tank Dempsey kill quips (plr0, zombie_cosmodrome / Nacht). Variety:
        // kill_close most of the time, kill_headshot on a headshot kill, kill_streak
        // every 5th kill burst, kill_ray/kill_crawler/kill_explo/kill_insta on the
        // matching special kills. At most one line every 2.5 s.
        static DWORD lastVox = 0;
        static int closeIdx = 0, headIdx = 0, streakIdx = 0, thunderIdx = 0;
        static int crawlerIdx = 0, exploIdx = 0, instaIdx = 0, damagedIdx = 0;
        if (GetTickCount() - lastVox > 2500 && !g_powerupSpawnedThisKill) {
            lastVox = GetTickCount();
            char vox[64];
            const char* cat = "kill_close";
            int maxN = 9, *pIdx = &closeIdx;
            if (g_kills > 0 && (g_kills % 10) == 0) { cat = "kill_thunder"; maxN = 5; pIdx = &thunderIdx; }
            else if (deadNow >= 3) { cat = "kill_streak"; maxN = 10; pIdx = &streakIdx; }
            else if (h.hitmarkerKill) { cat = "kill_headshot"; maxN = 10; pIdx = &headIdx; }
            snprintf(vox, sizeof(vox), "dempsey_%s_%d.wav", cat, (*pIdx) % maxN);
            (*pIdx) = ((*pIdx) + 1) % maxN;
            PlayGameSound(vox);
            Log("vox: %s (kills=%d, deadNow=%d)", vox, g_kills, deadNow);
        }
    }
    {
        Vec3 pp{};
        const bool havePos = ReadPlayerPos(&pp);
        const DWORD nowT = GetTickCount();
        bool anyDrop = false;
        for (DroppedPowerup& d : g_drops) {
            if (!d.active) continue;
            if (nowT - d.born > 30000) { d.active = false; continue; }
            anyDrop = true;
            if (havePos && nowT - d.born > 1500) {
                const float dx = pp.x - d.x, dz = pp.z - d.z;
                if (dx * dx + dz * dz < 400.0f * 400.0f) {
                    d.active = false;
                    PowerupCollect(d.type);
                }
            }
        }
        if (!anyDrop) mciSendStringA("close halo", nullptr, 0, nullptr);
        for (int i = 0; i < 5; ++i) {
            const DWORD endT = g_powerupEnd[i];
            h.powerupTime[i] = endT > nowT ? (int)((endT - nowT) / 1000) : 0;
        }
        for (int i = 0; i < 8; ++i) {
            h.drops[i].active = g_drops[i].active;
            h.drops[i].type = g_drops[i].type;
            h.drops[i].x = g_drops[i].x;
            h.drops[i].y = g_drops[i].y;
            h.drops[i].z = g_drops[i].z;
            h.drops[i].age = g_drops[i].active ? (float)(nowT - g_drops[i].born) / 1000.0f : 0.0f;
        }
    }
    h.kills = g_kills;
    h.spawnIntervalMs = Zombies_SpawnInterval(h.round);
    h.roundSize = Zombies_RoundSize(h.round);
    h.roundKills = h.kills - Zombies_RoundStartKills(h.round);

    static bool loggedInventory = false;
    static int loggedRound = 0;
    if (!loggedInventory && h.ammoValid) {
        Log("HUD: ItemMgr ok itemId=%04X weaponNo=%02X ammo=%d/%d", h.weaponId,
            h.weaponNo, h.ammoLoaded, h.ammoReserve);
        loggedInventory = true;
    }
    if (h.round != loggedRound) {
        Log("HUD: round=%d kills=%d alive=%d/%d maxAlive=%d interval=%dms itemId=%04X ammo=%d/%d",
            h.round, h.kills, h.alive, h.maxAlive, h.maxAlive, h.spawnIntervalMs,
            h.weaponId, h.ammoLoaded, h.ammoReserve);
        loggedRound = h.round;
    }

    const DWORD now = GetTickCount();

    // The scheduled perk jingle fires once the gulp has had its moment.
    // (removed: the sting now plays immediately when the perk is bought)

    // The delayed round start sting: BO1 waits the between-round time first.
    if (g_pendingRoundStart && now >= g_pendingRoundStartAt) {
        MciPlay("round", "bo1_round_start.wav");
        Log("round %d start sting (delayed to intermission end)", g_pendingRoundStart);
        g_pendingRoundStart = 0;
    }

    if (g_lastRound == 0 || h.round != g_lastRound) {
        const int previousRound = g_lastRound;
        g_bannerUntil = now + 2600;
        g_lastRound = h.round;
        // RoundForKills() advances the displayed round as soon as its kill
        // quota is reached, so the old quotaReached test could never fire.
        // Detect the transition itself and play the user-provided full round
        // change sound, then leave the ten-second BO1 intermission before the
        // next round-start sting.
        if (previousRound > 0 && h.round > previousRound) {
            g_roundCompleteUntil = now + (DWORD)kZombieBetweenRoundTime * 1000;
            g_intermissionUntil = g_roundCompleteUntil;
            MciPlay("round", "bo1_round_change.mp3");
            Log("round %d complete (kills=%d size=%d)", previousRound, h.kills,
                Zombies_RoundSize(previousRound));
        }
        if (TimeActive(g_intermissionUntil)) {
            g_pendingRoundStart = h.round;
            g_pendingRoundStartAt = g_intermissionUntil;
        } else {
            MciPlay("round", "bo1_round_start.wav");
        }
        Log("round %d start (size=%d hp=%d delay=%dms speed=%.0f)",
            h.round, h.roundSize, Zombies_ZombieHealth(h.round),
            h.spawnIntervalMs, Zombies_MoveSpeed(h.round));
    }

    if (g_hits > g_lastHits) {
        // Damage hit: BO1 shows a white cross marker.
        g_hitmarkerUntil = now + 130;
    }
    if (h.kills > g_lastKills) {
        g_killmarkerUntil = now + 240;
    }
    // BO1 score_highlight: every award, the +10 of a hit and the +50 of a kill
    // alike, pops in yellow next to the score and drifts up-left while fading
    // (_zombiemode_score.gsc:588). Merging them into one popup keeps the score
    // corner calm when a shotgun hit kills.
    if (h.points > g_lastPoints) {
        h.pointsPopup = h.points - g_lastPoints;
        g_pointsUntil = now + 850;
        g_popupBorn = now;
        g_popupDx = -(20.0f + (float)(GetTickCount() % 40));
        g_popupDy = (float)((int)(GetTickCount() % 30) - 15);
    }
    g_lastPoints = h.points;
    if (h.kills < g_lastKills) {
        // Room reload / new run.
        g_bannerUntil = now + 2600;
        g_lastRound = h.round;
        g_hitmarkerUntil = g_killmarkerUntil = g_pointsUntil = 0;
    }
    g_lastKills = h.kills;
    g_lastHits = g_hits;

    // Perk badges: owned flags and the pickup pop (0.4 s ease-out), for the HUD.
    for (int i = 0; i < 4; ++i) {
        h.perkOwned[i] = g_perkOwned[i];
        float pop = 0.0f;
        if (g_perkOwned[i]) {
            if (g_perkBoughtAt[i]) {
                const float t = (float)(now - g_perkBoughtAt[i]) / 600.0f;
                pop = t >= 1.0f ? 1.0f : 0.05f + 0.95f * t * t * (3.0f - 2.0f * t);
            } else {
                pop = 1.0f;
            }
        }
        h.perkPop[i] = pop;
    }
    h.perkMenu = g_perkMenu;
    h.packaPunched = g_packaPunched;
    h.packaWeaponId = g_packaWeaponId;
    h.boxSpinning = g_boxSpinning;
    h.boxWaiting = g_boxWaiting;
    h.boxSpin = g_boxSpinning ? (float)(GetTickCount() - g_boxSpinStart) / 2500.0f : 0.0f;
    h.boxResult = g_boxResult;

    h.hitmarker = TimeActive(g_hitmarkerUntil);
    h.hitmarkerKill = TimeActive(g_killmarkerUntil);
    h.roundBanner = TimeActive(g_bannerUntil);
    h.roundComplete = TimeActive(g_roundCompleteUntil);
    h.roundIntermission = TimeActive(g_intermissionUntil);
    // BO1 pulses the corner round number during the round-over animation
    // (chalk_round_over, _zombiemode.gsc:3230): a 0.5 s fade in/out loop.
    h.roundPulse = 0.0f;
    if (h.roundIntermission) {
        const float phase = (float)(GetTickCount() % 1000) / 1000.0f;
        h.roundPulse = 0.5f + 0.5f * sinf(phase * 6.2831853f);
    }
    if (!TimeActive(g_pointsUntil)) h.pointsPopup = 0;

    // Popup animation: BO1 moves the "+N" over 0.5 s and fades it out halfway.
    h.popupAge = 0.0f;
    h.popupDx = 0.0f;
    h.popupDy = 0.0f;
    if (h.pointsPopup > 0 && g_popupBorn != 0) {
        h.popupAge = (float)(now - g_popupBorn) / 1000.0f;
        const float t = h.popupAge / 0.5f;
        if (t > 1.0f) {
            h.popupDx = g_popupDx;
            h.popupDy = g_popupDy;
        } else {
            h.popupDx = g_popupDx * t;
            h.popupDy = g_popupDy * t;
        }
    }

    g_hud = h;
}

const ZombiesHud& Zombies_Get() {
    return g_hud;
}
