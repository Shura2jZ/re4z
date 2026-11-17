#include "bo1guest.h"
#include "zombies.h"

#include <windows.h>
#include <cstdint>
#include "log.h"

#include <d3d9.h>
#include <cstdint>
#include <cstring>

namespace {

constexpr const char* kShmemName = "Local\\RE4Z_BO1";
constexpr uint32_t kMagic = 0x5A314F42;  // "BO1Z"

struct ShmemHeader {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    volatile uint32_t seq;
    uint32_t pad[11];
};

struct Vtx {
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v;
};

HANDLE g_map = nullptr;
uint8_t* g_ptr = nullptr;
uint32_t g_lastSeq = 0;
IDirect3DTexture9* g_tex = nullptr;
UINT g_texW = 0, g_texH = 0;

// Event channel (ASI -> runtime).
constexpr const char* kEventShmemName = "Local\\RE4Z_EV";
constexpr uint32_t kEventMagic = 0x56453452;  // "RE4V"
HANDLE g_eventMap = nullptr;
uint8_t* g_eventPtr = nullptr;
uint32_t g_eventSeq = 0;
bool g_prevLmbEvent = false;

bool EnsureMap() {
    if (g_map) return true;
    g_map = OpenFileMappingA(FILE_MAP_READ, FALSE, kShmemName);
    if (!g_map) return false;
    g_ptr = (uint8_t*)MapViewOfFile(g_map, FILE_MAP_READ, 0, 0, 0);
    if (!g_ptr) {
        CloseHandle(g_map);
        g_map = nullptr;
        return false;
    }
    Log("bo1guest: shared memory mapped");
    return true;
}

bool EnsureEventMap() {
    if (g_eventMap) return true;
    // Create the event shmem (ASI is the writer).
    g_eventMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                    0, 64, kEventShmemName);
    if (!g_eventMap) return false;
    g_eventPtr = (uint8_t*)MapViewOfFile(g_eventMap, FILE_MAP_WRITE, 0, 0, 64);
    if (!g_eventPtr) {
        CloseHandle(g_eventMap);
        g_eventMap = nullptr;
        return false;
    }
    // Initialize header.
    uint32_t* hdr = (uint32_t*)g_eventPtr;
    hdr[0] = kEventMagic;
    hdr[1] = 0;  // event_type
    hdr[2] = 0;  // seq
    Log("bo1guest: event shmem created");
    return true;
}

void WriteEvent(uint32_t eventType) {
    if (!EnsureEventMap()) return;
    uint32_t* hdr = (uint32_t*)g_eventPtr;
    hdr[0] = kEventMagic;
    hdr[1] = eventType;
    hdr[2] = ++g_eventSeq;
}

bool UploadFrame(IDirect3DDevice9* device, ShmemHeader* hdr) {
    if (hdr->width == 0 || hdr->height == 0) return false;
    if (hdr->width > 8192 || hdr->height > 8192) return false;
    if (hdr->stride < hdr->width * 4) return false;

    if (!g_tex || g_texW != hdr->width || g_texH != hdr->height) {
        if (g_tex) { g_tex->Release(); g_tex = nullptr; }
        HRESULT hr = device->CreateTexture(hdr->width, hdr->height, 1, D3DUSAGE_DYNAMIC,
                                           D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_tex, nullptr);
        if (FAILED(hr) || !g_tex) {
            Log("bo1guest: CreateTexture(%ux%u) failed %08X", hdr->width, hdr->height, (unsigned)hr);
            g_tex = nullptr;
            g_texW = g_texH = 0;
            return false;
        }
        g_texW = hdr->width;
        g_texH = hdr->height;
        Log("bo1guest: texture %ux%u", g_texW, g_texH);
    }

    D3DLOCKED_RECT lr = {};
    if (FAILED(g_tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD))) return false;
    const uint8_t* src = g_ptr + sizeof(ShmemHeader);
    for (UINT y = 0; y < hdr->height; ++y) {
        memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch,
               src + (size_t)y * hdr->stride, (size_t)hdr->width * 4);
    }
    g_tex->UnlockRect(0);
    return true;
}

} // namespace

void Bo1Guest_OnReset() {
    if (g_tex) { g_tex->Release(); g_tex = nullptr; }
    g_texW = g_texH = 0;
}

bool Bo1Guest_Render(IDirect3DDevice9* device, float viewW, float viewH) {
    // Detect fire (LMB) and write event to runtime.
    const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (lmb && !g_prevLmbEvent) {
        WriteEvent(1);  // fire
    }
    g_prevLmbEvent = lmb;

    // Detect reload: R key press.
    static bool prevR = false;
    const bool r = (GetAsyncKeyState('R') & 0x8000) != 0;
    if (r && !prevR) {
        WriteEvent(2);  // reload
    }
    prevR = r;

    // Detect ADS: right mouse button.
    static bool prevAds = false;
    const bool ads = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (ads != prevAds) {
        WriteEvent(ads ? 3 : 4);  // 3 = ADS on, 4 = ADS off
    }
    prevAds = ads;

    // Detect sprint: Left Shift held.
    static bool prevSprint = false;
    const bool sprint = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (sprint != prevSprint) {
        WriteEvent(sprint ? 5 : 6);  // 5 = sprint on, 6 = sprint off
    }
    prevSprint = sprint;

    // Detect jump: Space press.
    static bool prevJump = false;
    const bool jump = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (jump && !prevJump) {
        WriteEvent(7);  // jump
    }
    prevJump = jump;

    // Write current weapon ID to shmem (bytes 12-15) for viewmodel switching.
    {
        EnsureEventMap();
        ZombiesHud z = Zombies_Get();
        if (g_eventPtr) {
            uint32_t* ehdr = (uint32_t*)g_eventPtr;
            ehdr[3] = (uint32_t)z.weaponId;  // RE4 item ID for viewmodel switching
        }
    }

    if (!EnsureMap()) return false;
    ShmemHeader* hdr = (ShmemHeader*)g_ptr;
    if (!hdr) return false;
    if (hdr->magic != kMagic) return false;

    uint32_t seq = hdr->seq;
    if (seq != g_lastSeq) {
        g_lastSeq = seq;
        if (!UploadFrame(device, hdr)) return false;
    }
    if (!g_tex) return false;  // no frame texture yet

    Vtx v[4] = {
        { 0.0f,      0.0f,      0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 0.0f, 0.0f },
        { viewW,     0.0f,      0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, 0.0f },
        { viewW,     viewH,     0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, 1.0f },
        { 0.0f,      viewH,     0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 0.0f, 1.0f },
    };

    device->SetTexture(0, g_tex);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(Vtx));
    device->SetTexture(0, nullptr);
    return true;
}
