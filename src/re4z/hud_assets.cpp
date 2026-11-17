#include "hud_assets.h"
#include "log.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

struct DdsHeader {
    uint32_t magic;
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitch;
    uint32_t depth;
    uint32_t mipCount;
    uint32_t reserved1[11];
    uint32_t pfSize;
    uint32_t pfFlags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rMask;
    uint32_t gMask;
    uint32_t bMask;
    uint32_t aMask;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};

bool BuildAssetPath(char* out, size_t outSize, const char* relativePath) {
    char exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    char* slash = strrchr(exePath, '\\');
    if (!slash) return false;
    *slash = '\0';
    snprintf(out, outSize, "%s\\re4z\\assets\\%s", exePath, relativePath);
    return true;
}

IDirect3DTexture9* LoadDdsTexture(IDirect3DDevice9* device, const char* path) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        Log("hud_assets: cannot open %s", path);
        return nullptr;
    }

    DdsHeader hdr = {};
    if (fread(&hdr, sizeof(DdsHeader), 1, f) != 1) {
        fclose(f);
        return nullptr;
    }
    if (hdr.magic != 0x20534444) {
        fclose(f);
        Log("hud_assets: bad magic in %s", path);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 4 + 124, SEEK_SET);
    long dataSizeLong = fileSize - (4 + 124);
    if (dataSizeLong <= 0) {
        fclose(f);
        return nullptr;
    }

    std::vector<uint8_t> pixels(dataSizeLong);
    if (fread(pixels.data(), 1, dataSizeLong, f) != (size_t)dataSizeLong) {
        fclose(f);
        return nullptr;
    }
    fclose(f);

    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    if (hdr.pfFlags & 0x4) {
        if (hdr.fourCC == 0x35545844) fmt = D3DFMT_DXT5;
        else if (hdr.fourCC == 0x31545844) fmt = D3DFMT_DXT1;
        else if (hdr.fourCC == 0x33545844) fmt = D3DFMT_DXT3;
    } else if (hdr.pfFlags & 0x40) {
        if (hdr.rgbBitCount == 32) fmt = D3DFMT_A8R8G8B8;
        else if (hdr.rgbBitCount == 24) fmt = D3DFMT_R8G8B8;
    } else if (hdr.pfFlags & 0x2) {
        fmt = D3DFMT_A8;
    }

    if (fmt == D3DFMT_UNKNOWN) {
        Log("hud_assets: unsupported format in %s", path);
        return nullptr;
    }

    // Force D3DPOOL_MANAGED for all textures to avoid video memory fragmentation
    // when MW2RE4's render target (1280x720) consumes most of the VRAM.
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_MANAGED;

    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = device->CreateTexture(hdr.width, hdr.height, (hdr.mipCount > 1) ? hdr.mipCount : 1,
                                       usage, fmt, pool, &tex, nullptr);
    if (FAILED(hr) || !tex) {
        Log("hud_assets: CreateTexture failed %08X for %s", (unsigned)hr, path);
        return nullptr;
    }

    if (fmt == D3DFMT_DXT1 || fmt == D3DFMT_DXT3 || fmt == D3DFMT_DXT5) {
        D3DLOCKED_RECT locked = {};
        hr = tex->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD);
        if (SUCCEEDED(hr)) {
            memcpy(locked.pBits, pixels.data(), (size_t)dataSizeLong);
            tex->UnlockRect(0);
        }
    } else {
        D3DLOCKED_RECT locked = {};
        hr = tex->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD);
        if (SUCCEEDED(hr)) {
            uint32_t bpp = hdr.rgbBitCount / 8;
            uint32_t rowBytes = hdr.width * bpp;
            for (uint32_t y = 0; y < hdr.height; ++y) {
                memcpy((uint8_t*)locked.pBits + (size_t)y * locked.Pitch,
                       pixels.data() + (size_t)y * rowBytes, rowBytes);
            }
            tex->UnlockRect(0);
        }
    }

    Log("hud_assets: loaded %s (%ux%u, fmt=%d)", path, hdr.width, hdr.height, (int)fmt);
    return tex;
}