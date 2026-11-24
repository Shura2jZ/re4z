#include "d3d9hook.h"
#include "overlay.h"
#include "log.h"

#include <d3d9.h>
#include <cstdint>
#include <cstring>

namespace {

typedef IDirect3D9* (WINAPI* Direct3DCreate9Fn)(UINT);
typedef HRESULT (WINAPI* CreateDeviceFn)(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
                                         DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT (WINAPI* ResetFn)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT (WINAPI* PresentFn)(IDirect3DDevice9*, const RECT*, const RECT*,
                                    HWND, const RGNDATA*);
typedef HRESULT (WINAPI* StretchRectFn)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*,
                                        IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);

Direct3DCreate9Fn g_origDirect3DCreate9 = nullptr;
CreateDeviceFn g_origCreateDevice = nullptr;
ResetFn g_origReset = nullptr;
PresentFn g_origPresent = nullptr;
StretchRectFn g_origStretchRect = nullptr;
bool g_worldDrawnThisFrame = false;

bool g_d3d9ObjectHooked = false;
bool g_deviceHooked = false;
IDirect3DDevice9* g_device = nullptr;

void** FindIAT(HMODULE module, const char* wantedDll, const char* wantedFunction) {
    if (!module) return nullptr;
    auto* dos = (IMAGE_DOS_HEADER*)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return nullptr;

    auto* imp = (IMAGE_IMPORT_DESCRIPTOR*)((uint8_t*)module + dir.VirtualAddress);
    for (; imp->Name; ++imp) {
        const char* dll = (const char*)((uint8_t*)module + imp->Name);
        if (_stricmp(dll, wantedDll) != 0) continue;

        auto* firstThunk = (IMAGE_THUNK_DATA*)((uint8_t*)module + imp->FirstThunk);
        auto* originalThunk = (IMAGE_THUNK_DATA*)((uint8_t*)module +
            (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));

        for (; firstThunk->u1.Function; ++firstThunk, ++originalThunk) {
            if (originalThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto* byName = (IMAGE_IMPORT_BY_NAME*)((uint8_t*)module +
                                                   originalThunk->u1.AddressOfData);
            if (strcmp((const char*)byName->Name, wantedFunction) == 0) {
                return (void**)&firstThunk->u1.Function;
            }
        }
    }
    return nullptr;
}

HRESULT WINAPI HookPresent(IDirect3DDevice9* device, const RECT* src, const RECT* dst,
                           HWND destWindow, const RGNDATA* dirtyRegion) {
    // Reset the per-frame world-model flag (one StretchRect draw per frame).
    extern bool g_worldDrawnThisFrame;
    g_worldDrawnThisFrame = false;
    if (device && device == g_device) {
        static bool logged = false;
        if (!logged) { Log("HookPresent called, device=%p g_device=%p", device, g_device); logged = true; }
        // Draw world objects here, at Present: this is the last point in the
        // frame, so the GX camera matrices match the camera the scene was
        // rendered with. Drawing at StretchRect used a stale matrix and made
        // the models slide when the camera moved.
        Overlay_DrawWorldModels(device);
        Overlay_Render(device);
    }
    return g_origPresent(device, src, dst, destWindow, dirtyRegion);
}

// The game's 3D pass ends with a StretchRect OUT of the back buffer: at that
// moment the scene's depth is still bound, so world models drawn here are
// correctly hidden by the ground and walls (drawing at Present is too late).
HRESULT WINAPI HookStretchRect(IDirect3DDevice9* device, IDirect3DSurface9* src,
                               const RECT* srcRect, IDirect3DSurface9* dst,
                               const RECT* dstRect, D3DTEXTUREFILTERTYPE filter) {
    if (device == g_device && !g_worldDrawnThisFrame && src) {
        IDirect3DSurface9* bb = nullptr;
        if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
            if (bb == src) {  // end of the 3D pass — mark only, draw at Present
                g_worldDrawnThisFrame = true;
            }
            bb->Release();
        }
    }
    return g_origStretchRect(device, src, srcRect, dst, dstRect, filter);
}

HRESULT WINAPI HookReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    Overlay_OnReset();
    HRESULT hr = g_origReset(device, params);
    if (SUCCEEDED(hr)) {
        g_device = device;
    }
    return hr;
}

void HookDevice(IDirect3DDevice9* device) {
    if (!device) return;
    g_device = device;
    if (g_deviceHooked) return;

    void** vtable = *(void***)device;
    DWORD oldProtect = 0;
    if (!VirtualProtect(vtable, 35 * sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        Log("D3D9 device vtable: VirtualProtect failed %lu", GetLastError());
        return;
    }

    g_origReset = (ResetFn)vtable[16];
    g_origPresent = (PresentFn)vtable[17];
    g_origStretchRect = (StretchRectFn)vtable[34];
    vtable[16] = (void*)&HookReset;
    vtable[17] = (void*)&HookPresent;
    vtable[34] = (void*)&HookStretchRect;

    VirtualProtect(vtable, 35 * sizeof(void*), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), vtable, 35 * sizeof(void*));
    g_deviceHooked = true;
    Log("D3D9 device vtable hooked (Reset=%08X Present=%08X)",
        (unsigned)g_origReset, (unsigned)g_origPresent);
}

HRESULT WINAPI HookCreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE deviceType,
                                HWND focusWindow, DWORD behaviorFlags,
                                D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** deviceOut) {
    HRESULT hr = g_origCreateDevice(self, adapter, deviceType, focusWindow, behaviorFlags,
                                    params, deviceOut);
    if (SUCCEEDED(hr) && deviceOut && *deviceOut) {
        HookDevice(*deviceOut);
    }
    return hr;
}

void HookD3D9Object(IDirect3D9* d3d9) {
    if (!d3d9 || g_d3d9ObjectHooked) return;
    void** vtable = *(void***)d3d9;

    // IDirect3D9::CreateDevice is index 16. Patching the shared vtable catches every
    // IDirect3D9 instance (the game creates more than one while enumerating devices).
    DWORD oldProtect = 0;
    if (!VirtualProtect(vtable, 17 * sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        Log("D3D9 object vtable: VirtualProtect failed %lu", GetLastError());
        return;
    }

    g_origCreateDevice = (CreateDeviceFn)vtable[16];
    vtable[16] = (void*)&HookCreateDevice;

    VirtualProtect(vtable, 17 * sizeof(void*), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), vtable, 17 * sizeof(void*));
    g_d3d9ObjectHooked = true;
    Log("D3D9 object vtable hooked (CreateDevice=%08X)", (unsigned)g_origCreateDevice);
}

IDirect3D9* WINAPI HookDirect3DCreate9(UINT sdkVersion) {
    IDirect3D9* d3d9 = g_origDirect3DCreate9(sdkVersion);
    if (d3d9) {
        HookD3D9Object(d3d9);
    }
    return d3d9;
}

} // namespace

bool InstallD3D9Hook(HMODULE gameModule) {
    void** slot = FindIAT(gameModule, "d3d9.dll", "Direct3DCreate9");
    if (!slot) {
        Log("D3D9: Direct3DCreate9 IAT entry not found");
        return false;
    }

    g_origDirect3DCreate9 = (Direct3DCreate9Fn)*slot;
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        Log("D3D9: VirtualProtect(IAT) failed %lu", GetLastError());
        return false;
    }
    *slot = (void*)&HookDirect3DCreate9;
    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);

    Log("D3D9: IAT hooked at %08X (orig %08X)", (unsigned)slot,
        (unsigned)g_origDirect3DCreate9);
    return true;
}
