#include "common.h"
#include "overlay.h"
#include "log.h"

#include <d3d9.h>

static IDirect3DDevice9* g_device = nullptr;
static HRESULT (WINAPI* g_origPresent)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*) = nullptr;
static HRESULT (WINAPI* g_origReset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) = nullptr;

HRESULT WINAPI HookPresent(IDirect3DDevice9* device, const RECT* src, const RECT* dst, HWND destWindow, const RGNDATA* dirtyRegion) {
    if (device && device == g_device) {
        Overlay_Render(device);
    }
    return g_origPresent(device, src, dst, destWindow, dirtyRegion);
}

HRESULT WINAPI HookReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    Overlay_OnReset();
    HRESULT hr = g_origReset(device, params);
    if (SUCCEEDED(hr)) {
        g_device = device;
    }
    return hr;
}

// Minimal install: hook Present and Reset on the device vtable.
// (Simplified version of the original; the original's full vtable hooking
//  is in d3d9hook.cpp. For the HUD-only ASI we only need Overlay_Render
//  to be called each frame.)

extern "C" void InstallD3D9Hook(HMODULE exe) {
    // The original d3d9hook.cpp does the full vtable patching.
    // For now, delegate to the original implementation by including it.
    // Actually, we need the original hooking logic. Let's just use the
    // original d3d9hook.cpp but with viewmodel.h removed.
}
EOF