#include "camera.h"
#include "game.h"
#include "log.h"
#include "shooting.h"

#include <cstring>

namespace {

typedef void (__cdecl* CameraSetFn)(void* cam);

CameraSetFn g_origCameraSet = nullptr;
volatile LONG g_fpsEnabled = 1;
volatile LONG g_eyeHeightBits = 0;   // set at init: 1550.0f
volatile LONG g_fovBits = 0;         // set at init: 75.0f
int g_cameraLogCounter = 0;
float g_recoilPitch = 0.0f;
float g_recoilYaw = 0.0f;
bool g_prevLmbRecoil = false;
bool g_adsActive = false;

float FloatFromBits(LONG bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

LONG BitsFromFloat(float f) {
    LONG bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// The patched call is CameraControl::Move -> CameraSetOrientationUp(CAMERA*).
// CAMERA::param is at +0xA4 (Campos +0xA4, Target +0xB0, Roll +0xBC, Fovy +0xC0).
void __cdecl CameraSetHook(void* cam) {
    static bool prevF6 = false;
    bool f6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
    if (f6 && !prevF6) {
        LONG old = g_fpsEnabled;
        InterlockedExchange(&g_fpsEnabled, old ? 0 : 1);
        Log("FPS camera: %s", g_fpsEnabled ? "ON" : "OFF");
    }
    prevF6 = f6;

    const bool enabled = g_fpsEnabled != 0;
    Vec3 playerPos = {};
    const bool havePlayer = ReadPlayerPos(&playerPos);

    if (enabled && cam && havePlayer) {
        CameraPoint* cp = (CameraPoint*)((uint8_t*)cam + 0xA4);
        const float eye = FloatFromBits(g_eyeHeightBits);
        const float eyeX = playerPos.x;
        const float eyeY = playerPos.y + eye;
        const float eyeZ = playerPos.z;

        // Use the camera's existing aim point, but look at it from the player's eye.
        // The third-person camera is behind/above Leon, so using its own position for
        // the direction would bake that offset into the FPS pitch.
        float dx = cp->Target.x - eyeX;
        float dy = cp->Target.y - eyeY;
        float dz = cp->Target.z - eyeZ;
        float len2 = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(len2) || len2 < 0.000001f) {
            dx = cp->Target.x - cp->Campos.x;
            dy = cp->Target.y - cp->Campos.y;
            dz = cp->Target.z - cp->Campos.z;
            len2 = dx * dx + dy * dy + dz * dz;
        }
        if (std::isfinite(len2) && len2 > 0.000001f) {
            const float inv = 1.0f / std::sqrt(len2);
            cp->Campos.x = eyeX;
            cp->Campos.y = eyeY;
            cp->Campos.z = eyeZ;
            cp->Target.x = eyeX + dx * inv * 10000.0f;
            cp->Target.y = eyeY + dy * inv * 10000.0f;
            cp->Target.z = eyeZ + dz * inv * 10000.0f;
            cp->Roll = 0.0f;
            cp->Fovy = FloatFromBits(g_fovBits);
            SetPlayerVisible(false);
            SetPlayerWeaponsVisible(false);
            SetPlayerPartsVisible(false);

            // ADS (aim down sights): right mouse button.
            const bool rmb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            g_adsActive = rmb;
            if (rmb) {
                // Narrower FOV for ADS.
                cp->Fovy = 45.0f;
                // Slightly lower eye height (looking down the sights).
                // (Keep it subtle; the viewmodel handles the visual offset.)
            }

            // BO1-style recoil: pitch up on fire, decay over time.
            const bool lmbNow = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (lmbNow && !g_prevLmbRecoil) {
                g_recoilPitch = 0.025f;
                g_recoilYaw = ((float)(rand() % 200) / 100.0f - 1.0f) * 0.008f;
            }
            g_prevLmbRecoil = lmbNow;
            g_recoilPitch *= 0.88f;
            g_recoilYaw *= 0.85f;
            if (g_recoilPitch < 0.0001f) g_recoilPitch = 0.0f;
            if (fabsf(g_recoilYaw) < 0.0001f) g_recoilYaw = 0.0f;
            if (g_recoilPitch != 0.0f || g_recoilYaw != 0.0f) {
                float rx = cp->Target.x - cp->Campos.x;
                float ry = cp->Target.y - cp->Campos.y;
                float rz = cp->Target.z - cp->Campos.z;
                float rlen = std::sqrt(rx * rx + ry * ry + rz * rz);
                if (rlen > 0.001f) {
                    cp->Target.x = cp->Campos.x + rx + g_recoilYaw * rlen;
                    cp->Target.y = cp->Campos.y + ry + g_recoilPitch * rlen;
                    cp->Target.z = cp->Campos.z + rz;
                }
            }

            // Hand the frame's eye/forward to the BO1 shooting bridge. Runs on
            // the game thread inside CameraControl::move, like MW2-RE4 does.
            Vec3 eye = { eyeX, eyeY, eyeZ };
            Vec3 fwd = { dx * inv, dy * inv, dz * inv };
            Shooting_OnFrame(eye, fwd);

            if (g_cameraLogCounter < 4 || (g_cameraLogCounter % 300) == 0) {
                Log("cam fps=%d player=(%.1f %.1f %.1f) eye=(%.1f %.1f %.1f) dir=(%.3f %.3f %.3f) fov=%.1f",
                    g_cameraLogCounter, playerPos.x, playerPos.y, playerPos.z,
                    cp->Campos.x, cp->Campos.y, cp->Campos.z,
                    dx * inv, dy * inv, dz * inv, cp->Fovy);
            }
            ++g_cameraLogCounter;
        }
    } else if (!enabled) {
        SetPlayerVisible(true);
        SetPlayerWeaponsVisible(true);
        SetPlayerPartsVisible(true);
    }

    if (g_origCameraSet) {
        g_origCameraSet(cam);
    }
}

} // namespace

bool Camera_IsEnabled() {
    return g_fpsEnabled != 0;
}

bool InstallCameraHook(HMODULE gameModule) {
    g_eyeHeightBits = BitsFromFloat(1550.0f);
    g_fovBits = BitsFromFloat(75.0f);

    uintptr_t base = (uintptr_t)gameModule;
    uint8_t* site = (uint8_t*)(base + 0x2F7111);
    if (site[0] != 0xE8) {
        Log("camera hook: expected call at %08X, found %02X", (unsigned)site, site[0]);
        return false;
    }

    int32_t rel = 0;
    memcpy(&rel, site + 1, sizeof(rel));
    uintptr_t target = (uintptr_t)(site + 5) + (intptr_t)rel;
    g_origCameraSet = (CameraSetFn)target;
    Log("camera hook: call site=%08X target=%08X", (unsigned)site, (unsigned)target);

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("camera hook: VirtualProtect failed %lu", GetLastError());
        return false;
    }

    int32_t newRel = (int32_t)((uintptr_t)&CameraSetHook - ((uintptr_t)site + 5));
    // The original instruction is a 5-byte call (E8), not a jmp: keep the call so
    // the caller's `add esp, 4` and our hook's ret stay balanced.
    site[0] = 0xE8;
    memcpy(site + 1, &newRel, sizeof(newRel));
    VirtualProtect(site, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, 5);

    Log("camera hook: installed -> %08X", (unsigned)&CameraSetHook);
    return true;
}
