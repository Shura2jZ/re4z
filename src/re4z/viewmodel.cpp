#include "viewmodel.h"
#include "log.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

struct QuadVertex {
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v;
};

struct ColorVertex {
    float x, y, z, rhw;
    D3DCOLOR color;
};

IDirect3DTexture9* g_texture = nullptr;
UINT g_texW = 0;
UINT g_texH = 0;
bool g_loadFailed = false;
float g_recoil = 0.0f;
bool g_prevLmb = false;
DWORD g_flashUntil = 0;

bool BuildAssetPath(char* out, size_t outSize) {
    char exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    char* slash = strrchr(exePath, '\\');
    if (!slash) return false;
    *slash = '\0';
    snprintf(out, outSize, "%s\\re4z\\bo1_m1911.rgba", exePath);
    return true;
}

bool LoadTexture(IDirect3DDevice9* device) {
    if (g_texture) return true;
    if (g_loadFailed) return false;

    char path[MAX_PATH] = {};
    if (!BuildAssetPath(path, sizeof(path))) {
        g_loadFailed = true;
        return false;
    }

    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        Log("viewmodel: missing %s (using no viewmodel)", path);
        g_loadFailed = true;
        return false;
    }

    uint32_t w = 0, h = 0;
    if (fread(&w, sizeof(w), 1, f) != 1 || fread(&h, sizeof(h), 1, f) != 1 ||
        w == 0 || h == 0 || w > 4096 || h > 4096) {
        fclose(f);
        Log("viewmodel: bad header in %s", path);
        g_loadFailed = true;
        return false;
    }

    const size_t pixelBytes = (size_t)w * (size_t)h * 4;
    std::vector<uint8_t> pixels(pixelBytes);
    if (fread(pixels.data(), 1, pixelBytes, f) != pixelBytes) {
        fclose(f);
        Log("viewmodel: truncated pixels in %s", path);
        g_loadFailed = true;
        return false;
    }
    fclose(f);

    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = device->CreateTexture(w, h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                       D3DPOOL_DEFAULT, &tex, nullptr);
    if (FAILED(hr) || !tex) {
        Log("viewmodel: CreateTexture failed %08X", (unsigned)hr);
        g_loadFailed = true;
        return false;
    }

    D3DLOCKED_RECT locked = {};
    hr = tex->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD);
    if (FAILED(hr)) {
        tex->Release();
        Log("viewmodel: LockRect failed %08X", (unsigned)hr);
        g_loadFailed = true;
        return false;
    }
    for (uint32_t y = 0; y < h; ++y) {
        memcpy((uint8_t*)locked.pBits + (size_t)y * locked.Pitch,
               pixels.data() + (size_t)y * w * 4, (size_t)w * 4);
    }
    tex->UnlockRect(0);

    g_texture = tex;
    g_texW = w;
    g_texH = h;
    Log("viewmodel: loaded %s (%ux%u)", path, w, h);
    return true;
}

void DrawMuzzleFlash(IDirect3DDevice9* device, float mx, float my, float size) {
    const D3DCOLOR core = D3DCOLOR_ARGB(235, 255, 246, 200);
    const D3DCOLOR edge = D3DCOLOR_ARGB(160, 255, 184, 58);
    ColorVertex fan[7];
    fan[0] = { mx, my, 0.0f, 1.0f, core };
    for (int i = 0; i < 6; ++i) {
        const float a = (float)i * 3.14159265f / 3.0f;
        const float r = (i & 1) ? size * 0.45f : size;
        fan[i + 1] = { mx + cosf(a) * r, my + sinf(a) * r, 0.0f, 1.0f, edge };
    }
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 5, fan, sizeof(ColorVertex));
}

} // namespace

void ViewModel_OnReset() {
    if (g_texture) {
        g_texture->Release();
        g_texture = nullptr;
    }
    g_texW = g_texH = 0;
    g_loadFailed = false;
}

void ViewModel_Render(IDirect3DDevice9* device, float screenW, float screenH, bool active) {
    if (!active || !device) return;
    if (!LoadTexture(device)) return;

    const DWORD now = GetTickCount();
    const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (lmb && !g_prevLmb) {
        g_recoil = 1.0f;
        g_flashUntil = now + 65;
    }
    g_prevLmb = lmb;
    g_recoil *= 0.86f;
    if (g_recoil < 0.005f) g_recoil = 0.0f;

    // BO1 viewmodels sit low and to the right. This sprite is a real render of
    // the T5 1911 viewmodel plus the USMC viewhands in the idle pose, viewed
    // from behind the right hand. Its muzzle is at ~4.4% x / 5.2% y.
    const float targetH = screenH * 0.64f;
    const float scale = targetH / (float)g_texH;
    const float w = (float)g_texW * scale;
    const float h = targetH;

    const float t = (float)now;
    const float bobX = sinf(t * 0.0017f) * (3.0f + 2.0f * g_recoil);
    const float bobY = cosf(t * 0.0023f) * 2.0f - g_recoil * 16.0f;
    const float x = screenW * 0.56f - w * 0.04f + bobX + g_recoil * 4.0f;
    const float y = screenH - h + screenH * 0.12f + bobY;

    QuadVertex quad[4] = {
        { x,     y,     0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 0.0f, 0.0f },
        { x + w, y,     0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, 0.0f },
        { x + w, y + h, 0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, 1.0f },
        { x,     y + h, 0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 0.0f, 1.0f },
    };

    device->SetTexture(0, g_texture);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, quad, sizeof(QuadVertex));
    device->SetTexture(0, nullptr);

    if ((int32_t)(g_flashUntil - now) > 0) {
        // Muzzle is projected from the gun's tag_flash while making the sprite.
        const float mx = x + w * 0.044f;
        const float my = y + h * 0.052f;
        DrawMuzzleFlash(device, mx, my, h * 0.065f);
    }
}
