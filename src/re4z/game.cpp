#include "game.h"
#include "log.h"

#include <cstring>

namespace {

uintptr_t g_base = 0;
uintptr_t g_pGGlobal = 0;
uintptr_t g_pPLGlobal = 0;

bool MatchPattern(const uint8_t* p, const char* pattern, size_t len) {
    // pattern is space separated hex bytes, "??" wildcard.
    size_t pi = 0;
    for (size_t i = 0; i < len; ) {
        while (pattern[pi] == ' ') ++pi;
        if (!pattern[pi]) return false;
        unsigned value = 0;
        bool wildcard = false;
        if (pattern[pi] == '?') {
            wildcard = true;
            ++pi;
            if (pattern[pi] == '?') ++pi;
        } else {
            char hi = pattern[pi++];
            char lo = pattern[pi++];
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hex(hi), l = hex(lo);
            if (h < 0 || l < 0) return false;
            value = (unsigned)((h << 4) | l);
        }
        if (!wildcard && p[i] != (uint8_t)value) return false;
        ++i;
    }
    return true;
}

uintptr_t ResolveGlobalFromCode(uintptr_t codeVa, const char* pattern, size_t len) {
    const uint8_t* p = (const uint8_t*)codeVa;
    if (!MatchPattern(p, pattern, len)) {
        Log("pattern mismatch at %08X", (unsigned)codeVa);
        return 0;
    }
    if (p[0] != 0xA1) {
        Log("pattern at %08X does not start with A1", (unsigned)codeVa);
        return 0;
    }
    uint32_t addr = 0;
    memcpy(&addr, p + 1, sizeof(addr));
    return (uintptr_t)addr;
}

} // namespace

bool GameInit(HMODULE gameModule) {
    g_base = (uintptr_t)gameModule;
    if (!g_base) return false;

    g_pGGlobal = ResolveGlobalFromCode(
        g_base + 0x2D19D0,
        "A1 ?? ?? ?? ?? B9 FF FF FF 7F 21 48 60", 13);
    g_pPLGlobal = ResolveGlobalFromCode(
        g_base + 0x198074,
        "A1 ?? ?? ?? ?? D8 CC D8 C9 D8 CA", 11);

    // Fall back to the validated static offsets if the patterns ever stop matching.
    if (!g_pGGlobal) g_pGGlobal = g_base + 0x806F3C;
    if (!g_pPLGlobal) g_pPLGlobal = g_base + 0x857054;

    Log("game base=%08X pG=%08X pPL=%08X", (unsigned)g_base,
        (unsigned)g_pGGlobal, (unsigned)g_pPLGlobal);
    return true;
}

uintptr_t GameBase() { return g_base; }
uintptr_t GetPLGlobal() { return g_pPLGlobal; }
uintptr_t GetPGGlobal() { return g_pGGlobal; }

// A stale pPL/pG after a room or mode transition is the usual cause of an access
// violation inside an ASI. VirtualQuery is cheap enough for the few globals we
// touch per frame and lets us bail out before dereferencing freed memory.
bool IsRangeReadable(const void* ptr, size_t size) {
    if (!ptr || size == 0) return false;
    uintptr_t cur = (uintptr_t)ptr;
    const uintptr_t end = cur + size;
    if (end < cur) return false;
    while (cur < end) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery((const void*)cur, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        const uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (regionEnd <= cur) return false;
        cur = regionEnd;
    }
    return true;
}

void* GetGame() {
    if (!g_pGGlobal) return nullptr;
    void* pG = *(void**)g_pGGlobal;
    if (!IsRangeReadable(pG, 0x8350)) return nullptr;
    return pG;
}

void* GetPlayer() {
    if (!g_pPLGlobal) return nullptr;
    void* player = *(void**)g_pPLGlobal;
    if (!IsRangeReadable(player, 0x800)) return nullptr;
    return player;
}

bool ReadPlayerPos(Vec3* out) {
    void* player = GetPlayer();
    if (!player || !out) return false;
    // cCoord::pos is at +0x94 in the decompilation (cUnit 0x0C, cCoord fields).
    const float* pos = (const float*)((const uint8_t*)player + 0x94);
    out->x = pos[0];
    out->y = pos[1];
    out->z = pos[2];
    if (!std::isfinite(out->x) || !std::isfinite(out->y) || !std::isfinite(out->z)) return false;
    if (std::fabs(out->x) > 1.0e7f || std::fabs(out->y) > 1.0e7f || std::fabs(out->z) > 1.0e7f) return false;
    return true;
}

bool IsPlayerAlive() {
    void* player = GetPlayer();
    if (!player) return false;
    uint32_t beFlag = *(const uint32_t*)player;
    return (beFlag & 1) != 0;
}

void SetPlayerVisible(bool visible) {
    void* player = GetPlayer();
    if (!player) return;
    uint32_t* beFlag = (uint32_t*)player;
    if ((*beFlag & 1) == 0) return;
    if (visible) {
        *beFlag |= 2u;
    } else {
        *beFlag &= ~2u;
    }
}

void SetPlayerWeaponsVisible(bool visible) {
    void* player = GetPlayer();
    if (!player) return;

    // cPlayer::Wep at +0x788; cPlWep::m_pWep / m_pWepHand at +0x34 / +0x38.
    // The held weapon is a separate cObjWep model and stays visible even when the
    // player's own model is hidden, so it has to be hidden separately in first person.
    uint8_t* wep = *(uint8_t**)((uint8_t*)player + 0x788);
    if (wep && IsRangeReadable(wep, 0x40)) {
        const int offsets[2] = { 0x34, 0x38 };
        for (int off : offsets) {
            void* obj = *(void**)(wep + off);
            if (!obj || !IsRangeReadable(obj, 0x10)) continue;
            uint32_t* beFlag = (uint32_t*)obj;
            if ((*beFlag & 1) == 0) continue;  // not alive / already pooled
            if (visible) {
                *beFlag |= 2u;
            } else {
                *beFlag &= ~2u;
            }
        }
    }

    // MW2RE4 uses the same display flags in first person: bit 30 hides the
    // player's weapon and bit 16 hides its self-shadow. This covers the weapon
    // even when the game recreates the cObjWep after a room transition.
    void* pG = GetGame();
    if (pG) {
        // Present and the camera hook can touch this word on different threads.
        LONG* dispFlg = (LONG*)((uint8_t*)pG + 0x58);
        const LONG bits = (LONG)(0x40000000u | 0x00010000u);
        if (visible) {
            InterlockedAnd(dispFlg, ~bits);
        } else {
            InterlockedOr(dispFlg, bits);
        }
    }
}

// A player model is a cModel whose pModelInfo points to a linked list of
// cModelInfo parts (body, face, head, hair, eyes, hands, knife...). In
// trans.cpp each part is skipped when cModelInfo::be_flag bit 3 (8) is clear,
// and the constructor sets that bit for a visible part. Clearing it hides the
// part without touching matrices or scales. cPlayer::be_flag bit 1 is not
// enough on its own: the game can set it again, and the extra parts hang off
// cPlBody rather than the main model. Walking pModelInfo covers everything,
// while the cPlBody pointers are kept as a fallback.
void SetPlayerPartsVisible(bool visible) {
    struct HiddenPart {
        void* info;
        uint32_t original;
    };
    static HiddenPart hidden[160] = {};
    static int hiddenCount = 0;

    auto hideInfo = [](uint8_t* info) {
        if (!info || !IsRangeReadable(info, 0x20)) return;
        uint32_t* beFlag = (uint32_t*)info;
        for (int i = 0; i < hiddenCount; ++i) {
            if (hidden[i].info == info) return;
        }
        if (hiddenCount >= (int)(sizeof(hidden) / sizeof(hidden[0]))) return;
        HiddenPart& part = hidden[hiddenCount++];
        part.info = info;
        part.original = *beFlag;
    };

    if (!visible) {
        void* player = GetPlayer();
        if (!player) return;

        // Every part reachable from the cModel's pModelInfo chain (0x15C).
        uint8_t* info = *(uint8_t**)((uint8_t*)player + 0x15C);
        for (int i = 0; info && i < 128; ++i) {
            if (!IsRangeReadable(info, 0x20)) break;
            uint8_t* next = *(uint8_t**)(info + 0x14);
            hideInfo(info);
            info = next;
        }

        // cPlBody pointers too: some costumes keep parts outside the main chain.
        uint8_t* body = *(uint8_t**)((uint8_t*)player + 0x794);
        if (body && IsRangeReadable(body, 0xF0)) {
            const int offsets[] = { 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C };
            for (int off : offsets) {
                hideInfo(*(uint8_t**)(body + off));
            }
        }

        // Re-clear bit 3 every frame: the game may make a part visible again
        // (aiming, costume changes) while first person is active.
        for (int i = 0; i < hiddenCount; ++i) {
            uint8_t* part = (uint8_t*)hidden[i].info;
            if (!part || !IsRangeReadable(part, 0x10)) continue;
            *(uint32_t*)part &= ~8u;
        }

        static int lastLogged = -1;
        if (hiddenCount != lastLogged) {
            Log("player parts hidden: %d (body=%08X)", hiddenCount,
                (unsigned)(uintptr_t)body);
            lastLogged = hiddenCount;
        }
        return;
    }

    for (int i = 0; i < hiddenCount; ++i) {
        uint8_t* part = (uint8_t*)hidden[i].info;
        if (!part || !IsRangeReadable(part, 0x20)) continue;
        uint32_t* beFlag = (uint32_t*)part;
        if ((*beFlag & 1) == 0) continue;  // stale / recycled info
        // Restore only the visibility bit; the game may have changed others.
        if (hidden[i].original & 8u) {
            *beFlag |= 8u;
        } else {
            *beFlag &= ~8u;
        }
    }
    hiddenCount = 0;
}
