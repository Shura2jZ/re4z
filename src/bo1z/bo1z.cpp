// BO1Z guest renderer.
//
// The game presents via IDirect3DSwapChain9::Present. Its swap chain uses a
// STATIC vtable inside d3d9.dll (not a per-device heap copy), so we can hook it
// once and catch every present. We capture the front buffer and publish it to
// shared memory (Local\RE4Z_BO1) for the RE4 side to composite.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <psapi.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

void Log(const char* fmt, ...);

constexpr const char* kOutDir = "E:\\re4z\\build\\bo1z\\";
constexpr bool kHideWorld = false;  // world-hide code-patching attempts all failed; using depth buffer now

FILE* g_log = nullptr;
IDirect3DDevice9* g_device = nullptr;
IDirect3DSurface9* g_staging = nullptr;
UINT g_stagingW = 0, g_stagingH = 0;
D3DFORMAT g_stagingFmt = D3DFMT_UNKNOWN;

// depth capture (INTZ hack — D3D9 can't read D24S8 directly)
constexpr D3DFORMAT kIntzFmt = (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z');
IDirect3DTexture9* g_intzTex = nullptr;
IDirect3DSurface9* g_intzSurf = nullptr;
IDirect3DSurface9* g_depthStaging = nullptr;
UINT g_depthW = 0, g_depthH = 0;
float g_depthThreshold = 0.5f;

bool EnsureIntzDepth(IDirect3DDevice9* device, UINT w, UINT h) {
    if (g_intzTex && g_intzSurf && g_depthW == w && g_depthH == h) return true;
    if (g_intzSurf) { g_intzSurf->Release(); g_intzSurf = nullptr; }
    if (g_intzTex) { g_intzTex->Release(); g_intzTex = nullptr; }
    HRESULT hr = device->CreateTexture(w, h, 1, D3DUSAGE_DEPTHSTENCIL, kIntzFmt, D3DPOOL_DEFAULT, &g_intzTex, nullptr);
    if (FAILED(hr) || !g_intzTex) { Log("CreateTexture INTZ failed %08X", (unsigned)hr); return false; }
    g_intzTex->GetSurfaceLevel(0, &g_intzSurf);
    device->SetDepthStencilSurface(g_intzSurf);
    g_depthW = w; g_depthH = h;
    Log("INTZ depth surface set (%ux%u)", w, h);
    return true;
}

typedef HRESULT (WINAPI* SwapPresentFn)(IDirect3DSwapChain9*, const RECT*, const RECT*,
                                        HWND, const RGNDATA*, DWORD);
SwapPresentFn g_origSwapPresent = nullptr;
volatile LONG g_presentCount = 0;
volatile LONG g_dumps = 0;
volatile LONG g_dumping = 0;

HRESULT WINAPI HookSwapPresent(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);

void Log(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

void OpenLog() {
    if (g_log) return;
    CreateDirectoryA("E:\\re4z\\build", nullptr);
    CreateDirectoryA(kOutDir, nullptr);
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%sbo1z.log", kOutDir);
    g_log = fopen(path, "a");
    if (g_log) { SYSTEMTIME st; GetLocalTime(&st);
        Log("=== BO1Z %02d:%02d:%02d ===", st.wHour, st.wMinute, st.wSecond); }
}

struct ShmemHeader {
    uint32_t magic;      // 0x5A314F42 = "BO1Z"
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    volatile uint32_t seq;
    uint32_t pad[11];
};
constexpr uint32_t kShmemMagic = 0x5A314F42;
constexpr const char* kShmemName = "Local\\RE4Z_BO1";
HANDLE g_shmem = nullptr;
uint8_t* g_shmemPtr = nullptr;
uint32_t g_shmemSize = 0;

void PublishSharedMem(uint32_t w, uint32_t h, const uint8_t* bgra, int pitch,
                      const uint8_t* depth, int depthPitch) {
    uint32_t dataSize = w * h * 4;
    uint32_t total = (uint32_t)sizeof(ShmemHeader) + dataSize;
    if (!g_shmemPtr || g_shmemSize < total) {
        if (g_shmemPtr) { UnmapViewOfFile(g_shmemPtr); g_shmemPtr = nullptr; }
        if (g_shmem) { CloseHandle(g_shmem); g_shmem = nullptr; }
        g_shmem = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, total, kShmemName);
        if (!g_shmem) { Log("CreateFileMapping failed %lu", GetLastError()); return; }
        g_shmemPtr = (uint8_t*)MapViewOfFile(g_shmem, FILE_MAP_ALL_ACCESS, 0, 0, total);
        if (!g_shmemPtr) { Log("MapViewOfFile failed"); CloseHandle(g_shmem); g_shmem = nullptr; return; }
        g_shmemSize = total;
        ShmemHeader* hdr = (ShmemHeader*)g_shmemPtr;
        hdr->magic = kShmemMagic; hdr->width = w; hdr->height = h; hdr->stride = w * 4; hdr->seq = 0;
        Log("shared memory created (%ux%u, %u bytes)", w, h, total);
    }
    ShmemHeader* hdr = (ShmemHeader*)g_shmemPtr;
    hdr->width = w; hdr->height = h; hdr->stride = w * 4;
    uint8_t* dst = g_shmemPtr + sizeof(ShmemHeader);
    const float thresh = g_depthThreshold;
    static bool loggedSample = false;
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* s = bgra + (size_t)y * pitch;
        const uint8_t* ds = depth ? (depth + (size_t)y * depthPitch) : nullptr;
        uint8_t* d = dst + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t b = s[x * 4 + 0], g = s[x * 4 + 1], r = s[x * 4 + 2];
            uint8_t a;
            if (ds) {
                // INTZ: depth is stored in the green channel (8-bit).
                uint8_t dg = ds[x * 4 + 1];
                a = (dg < (uint8_t)(thresh * 255.0f)) ? 255 : 0;
                if (!loggedSample && y == h / 2 && x >= w / 2 && x < w / 2 + 4) {
                    Log("INTZ sample (%u,%u): B=%02X G=%02X R=%02X A=%02X -> alpha=%u",
                        x, y, ds[x*4+0], ds[x*4+1], ds[x*4+2], ds[x*4+3], a);
                }
            } else {
                uint8_t mx = r > g ? r : g; if (b > mx) mx = b;
                a = (mx > 8) ? 255 : 0;
            }
            d[x * 4 + 0] = b; d[x * 4 + 1] = g; d[x * 4 + 2] = r; d[x * 4 + 3] = a;
        }
    }
    if (depth && !loggedSample) loggedSample = true;
    InterlockedIncrement((volatile LONG*)&hdr->seq);
}

bool WriteBmp32(const char* path, const void* pixels, int w, int h, int pitch) {
    BITMAPFILEHEADER bf = {}; BITMAPINFOHEADER bi = {};
    bf.bfType = 0x4D42; bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bf.bfSize = bf.bfOffBits + (DWORD)w * (DWORD)h * 4;
    bi.biSize = sizeof(BITMAPINFOHEADER); bi.biWidth = w; bi.biHeight = -h;
    bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = BI_RGB; bi.biSizeImage = (DWORD)w * (DWORD)h * 4;
    HANDLE f = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, &bf, sizeof(bf), &written, nullptr) && WriteFile(f, &bi, sizeof(bi), &written, nullptr);
    const uint8_t* src = (const uint8_t*)pixels;
    for (int y = 0; ok && y < h; ++y) ok = WriteFile(f, src + (size_t)y * pitch, (DWORD)w * 4, &written, nullptr) == TRUE;
    CloseHandle(f); return ok;
}

void CaptureAndPublish(IDirect3DDevice9* device) {
    IDirect3DSurface9* back = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) { Log("GetBackBuffer failed"); return; }
    D3DSURFACE_DESC desc = {};
    if (FAILED(back->GetDesc(&desc))) { back->Release(); return; }
    if (!g_staging || g_stagingW != desc.Width || g_stagingH != desc.Height || g_stagingFmt != desc.Format) {
        if (g_staging) { g_staging->Release(); g_staging = nullptr; }
        if (FAILED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &g_staging, nullptr))) {
            Log("CreateOffscreenPlainSurface failed"); back->Release(); return;
        }
        g_stagingW = desc.Width; g_stagingH = desc.Height; g_stagingFmt = desc.Format;
        Log("staging %ux%u fmt=%d", desc.Width, desc.Height, (int)desc.Format);
    }
    HRESULT hr = device->GetRenderTargetData(back, g_staging);
    const char* method = "backbuffer";
    if (FAILED(hr)) {
        hr = device->GetFrontBufferData(0, g_staging);
        method = "frontbuffer";
    }
    back->Release();
    if (FAILED(hr)) { Log("capture failed %08X", (unsigned)hr); return; }

    // Capture the depth buffer via INTZ (viewmodel near, world far).
    const uint8_t* depthBits = nullptr;
    int depthPitch = 0;
    if (EnsureIntzDepth(device, desc.Width, desc.Height)) {
        if (!g_depthStaging) {
            if (SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, kIntzFmt, D3DPOOL_SYSTEMMEM, &g_depthStaging, nullptr))) {
                Log("depth staging %ux%u (INTZ)", desc.Width, desc.Height);
            }
        }
        if (g_depthStaging) {
            HRESULT dhr = device->GetRenderTargetData(g_intzSurf, g_depthStaging);
            if (FAILED(dhr)) {
                if (g_presentCount % 120 == 0) Log("depth GetRenderTargetData failed %08X", (unsigned)dhr);
            } else {
                D3DLOCKED_RECT dlr = {};
                if (SUCCEEDED(g_depthStaging->LockRect(&dlr, nullptr, D3DLOCK_READONLY))) {
                    depthBits = (const uint8_t*)dlr.pBits;
                    depthPitch = (int)dlr.Pitch;
                }
            }
        }
    }

    D3DLOCKED_RECT lr = {};
    if (FAILED(g_staging->LockRect(&lr, nullptr, D3DLOCK_READONLY))) { Log("LockRect failed"); return; }
    PublishSharedMem(g_stagingW, g_stagingH, (const uint8_t*)lr.pBits, (int)lr.Pitch, depthBits, depthPitch);
    if (g_dumps < 6 && (g_presentCount % 120) == 0) {
        InterlockedIncrement(&g_dumps);
        char path[MAX_PATH]; snprintf(path, sizeof(path), "%sbo1z_frame.bmp", kOutDir);
        bool ok = WriteBmp32(path, lr.pBits, (int)g_stagingW, (int)g_stagingH, (int)lr.Pitch);
        Log("%s %s", ok ? "dumped" : "WRITE FAILED", method);
    }
    g_staging->UnlockRect();
    if (depthBits && g_depthStaging) g_depthStaging->UnlockRect();
}

HRESULT WINAPI HookSwapPresent(IDirect3DSwapChain9* sc, const RECT* src, const RECT* dst,
                               HWND hwnd, const RGNDATA* dirty, DWORD flags) {
    IDirect3DDevice9* dev = nullptr;
    sc->GetDevice(&dev);
    if (dev) {
        if (!g_device) { g_device = dev; Log("first SwapPresent, device %p", (void*)dev); }
        LONG n = InterlockedIncrement(&g_presentCount);
        if (g_presentCount == 1) Log("first Present (swap chain)");
        if (dev == g_device && (n % 4) == 0 && InterlockedCompareExchange(&g_dumping, 1, 0) == 0) {
            CaptureAndPublish(dev);
            InterlockedExchange(&g_dumping, 0);
        }
        dev->Release();
    }
    return g_origSwapPresent(sc, src, dst, hwnd, dirty, flags);
}

// ---- world-hide: render an empty world ----
// The world scene fills the view-params buffer via 0x6c8090: view params first,
// then a _memcpy of the world geometry (0x10bc bytes at buffer+0xcb). We redirect
// the 0x6c8090 call to this stub, which runs the original fill (valid view params,
// no null pointers) and then ZEROES only the geometry region — so 0x6c6450 renders
// an empty world without crashing.
void __attribute__((stdcall)) WorldSetupStub(void* refdef, void* buffer) {
    typedef void (__attribute__((stdcall)) *SetupFn)(void*, void*);
    ((SetupFn)0x6C8090)(refdef, buffer);          // original fill (valid view params)
    memset((uint8_t*)buffer + 0xcb, 0, 0x10bc);   // zero the world geometry region
}

void PatchWorldGeometry() {
    // Redirect CALL 0x6c8090 (at 0x6c8da1) to WorldSetupStub.
    uint8_t* p = (uint8_t*)0x6C8DA1;
    if (p[0] != 0xE8) { Log("not a CALL at 0x6c8da1 (was %02X)", p[0]); return; }
    uintptr_t target = (uintptr_t)&WorldSetupStub;
    int32_t rel = (int32_t)(target - (uintptr_t)(p + 5));
    DWORD oldProtect = 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) { Log("vprotect failed %lu", GetLastError()); return; }
    p[0] = 0xE8;
    *(int32_t*)(p + 1) = rel;
    VirtualProtect(p, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    Log("world buffer fill redirected to memset stub (stub=%08X rel=%d)", (unsigned)target, (int)rel);
}

bool HookSwapChainStatic() {
    // The game's swap chain uses d3d9.dll's static IDirect3DSwapChain9 vtable
    // (RVA 0x2514), which is shared. Hook its Present (index 3) directly — no
    // dummy device (HAL fails while the game holds the GPU) and no memory scan.
    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    if (!d3d9) d3d9 = LoadLibraryA("d3d9.dll");
    if (!d3d9) { Log("d3d9 not loaded"); return false; }
    MODULEINFO mi = {};
    if (!GetModuleInformation(GetCurrentProcess(), d3d9, &mi, sizeof(mi))) { Log("GetModuleInformation failed"); return false; }
    uintptr_t base = (uintptr_t)mi.lpBaseOfDll;
    uintptr_t hi = base + mi.SizeOfImage;
    void** svt = (void**)(base + 0x2514);  // static swap chain vtable
    uintptr_t present = (uintptr_t)svt[3];
    // sanity: Present must be inside d3d9.dll and non-zero
    if (!present || present < base || present >= hi) {
        Log("swap chain vtable sanity failed (present=%08X, d3d9=%08X..%08X)", (unsigned)present, (unsigned)base, (unsigned)hi);
        return false;
    }
    g_origSwapPresent = (SwapPresentFn)present;
    DWORD oldProtect = 0;
    if (!VirtualProtect(&svt[3], sizeof(void*), PAGE_READWRITE, &oldProtect)) { Log("vprotect failed %lu", GetLastError()); return false; }
    svt[3] = (void*)&HookSwapPresent;
    VirtualProtect(&svt[3], sizeof(void*), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), &svt[3], sizeof(void*));
    Log("swap chain vtable %p present=%08X hooked", (void*)svt, (unsigned)present);
    return true;
}

DWORD WINAPI MonitorThread(LPVOID) {
    for (;;) { Sleep(3000); Log("alive present=%d", (int)g_presentCount); }
}

DWORD WINAPI InitThread(LPVOID) {
    OpenLog();
    if (kHideWorld) PatchWorldGeometry();
    HookSwapChainStatic();
    Log("init done");
    CreateThread(nullptr, 0, MonitorThread, nullptr, 0, nullptr);
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        HANDLE th = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (th) CloseHandle(th);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_staging) { g_staging->Release(); g_staging = nullptr; }
        if (g_log) { fclose(g_log); g_log = nullptr; }
    }
    return TRUE;
}
