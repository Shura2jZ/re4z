#include "shooting.h"
#include "game.h"
#include "log.h"

#include <cstring>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

namespace {

bool BuildSoundPath(char* out, size_t outSize, const char* filename) {
    char exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    char* slash = strrchr(exePath, '\\');
    if (!slash) return false;
    *slash = '\0';
    snprintf(out, outSize, "%s\\re4z\\assets\\sounds\\%s", exePath, filename);
    return true;
}

// Authentic BO1 weapon fire sounds (extracted from common_mp.ff via Tom Crowley tool).
// Mapped by RE4 item id (ItemMgr +0x10), which is what identifies the equipped weapon.
const char* SoundFileForItem(int itemId) {
    switch (itemId) {
    // handguns
    case 0x23: return "bo1_colt.wav";   // handgun
    case 0x24: return "bo1_cz75.wav";   // Blacktail -> CZ75
    // magnums
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x3E: return "bo1_cz75.wav";
    // shotguns
    case 0x25: case 0x26: return "bo1_spas.wav";    // shotgun
    case 0x27: case 0x28: return "bo1_spas.wav";    // striker
    // handguns (Red9 / Punisher)
    case 0x21: return "bo1_colt.wav";
    case 0x22: return "bo1_cz75.wav";
    // rifles
    case 0x29: return "bo1_m14.wav";               // rifle (semi-auto)
    case 0x2A: return "bo1_m14.wav";                // rifle (bolt-action)
    case 0x2C: return "bo1_ak47.wav";               // Chicago Typewriter -> AK
    // SMG / TMP
    case 0x2E: case 0x6B: case 0x99: case 0x2F: case 0x6C: case 0x51: return "bo1_uzi.wav";
    // rocket
    case 0x34: return "bo1_rpg.wav";
    default: return "bo1_colt.wav";
    }
}

// RE4 UHD 1.1.0 (bio4.exe) addresses, validated offline on this exact binary.
// RVA 0x286C40 is RE4's BulletHit(attacker, start, end, weaponNo, flags, f) and
// RVA 0x7865 is the shared jmp thunk every Leon weapon reaches it through.
constexpr uintptr_t kBulletHitRva = 0x286C40;
constexpr uintptr_t kBulletHitThunkRva = 0x7865;
constexpr uint8_t kBulletHitPrologue[9] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00 };

using BulletHitFn = int(__cdecl*)(void* attacker, Vec3* start, Vec3* end, int weaponNo, int flags, float f);

BulletHitFn g_bulletHit = nullptr;
bool g_enabled = true;
bool g_prevLmb = false;
bool g_prevToggle = false;
DWORD g_lastFire = 0;
Vec3 g_eye = {};
Vec3 g_dir = { 0.0f, 0.0f, 1.0f };
uint32_t g_shotSeq = 0;
uint32_t g_hits = 0;

// Reads Leon's equipped item id from cItemMgr (same read the HUD uses).
int ReadItemId() {
    const uintptr_t im = GameBase() + 0x870FD4;
    if (!im || !IsRangeReadable((const void*)im, 0x30)) return 0;
    return (int)*(const uint16_t*)((const uint8_t*)im + 0x10);
}

// RE4 weapon numbers (pG->weapon_no space), mapped from the equipped item id.
int Re4WeaponNumber(int itemId) {
    switch (itemId) {
    case 0x23: case 0x24: return 0x01;                    // handgun
    case 0x29: case 0x2A: return 0x09;                    // rifle
    case 0x2C: return 0x0C;                               // Chicago Typewriter
    case 0x21: case 0x22: return 0x01;                    // handgun (Red9/Punisher)
    case 0x25: case 0x26: return 0x07;                    // shotgun
    case 0x27: case 0x28: return 0x08;                    // striker
    case 0x2E: case 0x6B: case 0x99: case 0x2F: case 0x6C: case 0x51: return 0x0B; // tmp/smg
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x3E: return 0x02; // magnum
    case 0x34: return 0x0D;                               // rocket
    case 0x35: return 0x10;                               // knife
    default: return 0x01;
    }
}

// BO1-ish cadence per RE4 weapon number.
DWORD CadenceMs(int weapon) {
    switch (weapon) {
    case 0x0B: return 75;    // TMP / SMG (full auto)
    case 0x0C: return 70;    // Chicago
    case 0x07: case 0x08: return 220; // shotgun / striker
    case 0x09: case 0x0A: return 120; // rifle
    case 0x02: return 650;   // magnum
    case 0x0D: return 1000;  // rocket
    case 0x10: return 400;   // knife
    default: return 120;     // handgun
    }
}

bool FullAuto(int weapon) {
    return weapon == 0x0B || weapon == 0x0C;
}

// The thunk is the single point all Leon weapons jump through. RE4's own shot
// (attacker == Leon) is dropped here: the ASI fires the BO1 shot itself and
// passes the same BulletHit, so damage, blood and stagger stay RE4's.
int __cdecl BulletHitThunkHook(void* attacker, Vec3* start, Vec3* end, int weaponNo, int flags, float f) {
    if (g_enabled && attacker && attacker == GetPlayer()) {
        return 0;
    }
    return g_bulletHit(attacker, start, end, weaponNo, flags, f);
}

void Fire(int weapon) {
    void* pl = GetPlayer();
    if (!pl) return;
    // Muzzle offset: start slightly forward from eye to avoid starting inside the player.
    Vec3 start = {
        g_eye.x + g_dir.x * 50.0f,
        g_eye.y + g_dir.y * 50.0f,
        g_eye.z + g_dir.z * 50.0f,
    };
    Vec3 end = {
        g_eye.x + g_dir.x * 50000.0f,
        g_eye.y + g_dir.y * 50000.0f,
        g_eye.z + g_dir.z * 50000.0f,
    };
    const int hits = g_bulletHit(pl, &start, &end, weapon, 0, 6000.0f);
    ++g_shotSeq;
    if (hits > 0) ++g_hits;

    // Play authentic BO1 weapon fire sound (selected by equipped item id).
    const int sndItem = ReadItemId();
    char soundPath[MAX_PATH] = {};
    if (BuildSoundPath(soundPath, sizeof(soundPath), SoundFileForItem(sndItem ? sndItem : 0))) {
        PlaySoundA(soundPath, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    } else {
        Beep(800, 30);
    }

    if (g_shotSeq <= 20 || hits > 0) {
        Log("shot %u: weapon 0x%02X -> BulletHit %d (hits total %u) dir=(%.3f %.3f %.3f)",
            g_shotSeq, weapon, hits, g_hits, g_dir.x, g_dir.y, g_dir.z);
    }
}

} // namespace

bool Shooting_Init(HMODULE gameModule) {
    const uintptr_t base = (uintptr_t)gameModule;
    if (!base) return false;

    const uint8_t* prologue = (const uint8_t*)(base + kBulletHitRva);
    if (memcmp(prologue, kBulletHitPrologue, sizeof(kBulletHitPrologue)) != 0) {
        Log("shooting: BulletHit prologue mismatch, disabled");
        return false;
    }
    g_bulletHit = (BulletHitFn)(base + kBulletHitRva);

    Log("shooting: bridge ENABLED by default (F10 toggles). BulletHit at %08X", (unsigned)(uintptr_t)g_bulletHit);

    uint8_t* thunk = (uint8_t*)(base + kBulletHitThunkRva);
    if (thunk[0] != 0xE9) {
        Log("shooting: BulletHit thunk is not a jmp (0x%02X), native shots kept", thunk[0]);
    } else {
        int32_t rel = 0;
        memcpy(&rel, thunk + 1, sizeof(rel));
        const uintptr_t target = (uintptr_t)(thunk + 5) + (intptr_t)rel;
        if (target != (uintptr_t)g_bulletHit) {
            Log("shooting: thunk target %08X != BulletHit %08X, native shots kept",
                (unsigned)target, (unsigned)(uintptr_t)g_bulletHit);
        } else {
            DWORD oldProtect = 0;
            if (VirtualProtect(thunk, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                const int32_t newRel =
                    (int32_t)((uintptr_t)&BulletHitThunkHook - ((uintptr_t)thunk + 5));
                thunk[0] = 0xE9;
                memcpy(thunk + 1, &newRel, sizeof(newRel));
                VirtualProtect(thunk, 5, oldProtect, &oldProtect);
                FlushInstructionCache(GetCurrentProcess(), thunk, 5);
                Log("shooting: native Leon BulletHit neutralised (thunk %08X -> hook %08X)",
                    (unsigned)(uintptr_t)thunk, (unsigned)(uintptr_t)&BulletHitThunkHook);
            } else {
                Log("shooting: VirtualProtect failed %lu (native shots kept)", GetLastError());
            }
        }
    }

    Log("shooting: BulletHit at %08X, enabled=%d", (unsigned)(uintptr_t)g_bulletHit, g_enabled ? 1 : 0);
    return true;
}

void Shooting_OnFrame(const Vec3& eye, const Vec3& dir) {
    // F10 toggles the bridge (useful to compare against RE4's own shot).
    const bool toggle = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (toggle && !g_prevToggle) {
        g_enabled = !g_enabled;
        Log("shooting bridge: %s", g_enabled ? "ON" : "OFF");
    }
    g_prevToggle = toggle;

    const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    if (!g_bulletHit || !g_enabled) {
        g_prevLmb = lmb;
        return;
    }

    g_eye = eye;
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len > 0.000001f) {
        g_dir.x = dir.x / len;
        g_dir.y = dir.y / len;
        g_dir.z = dir.z / len;
    }

    const int itemId = ReadItemId();
    static int s_lastItemId = 0;
    if (itemId != 0 && itemId != s_lastItemId) {
        s_lastItemId = itemId;
        Log("weapon switch: itemId=%04X", itemId);
    }
    const int weapon = Re4WeaponNumber(itemId);
    const DWORD now = GetTickCount();
    const bool fireEdge = lmb && (!g_prevLmb || FullAuto(weapon));
    if (fireEdge && now - g_lastFire >= CadenceMs(weapon)) {
        g_lastFire = now;
        Fire(weapon);
    }
    g_prevLmb = lmb;
}
