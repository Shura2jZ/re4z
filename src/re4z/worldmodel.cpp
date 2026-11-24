// worldmodel.cpp: draw a static .r4m model (boshjerns format, v2) inside RE4's
// own 3D scene at a fixed world position, using the game's render camera.
// Technique ported from boshjerns' re4-boshjerns-sdk (MIT) mesh_model.cpp and
// own_model.cpp: camera matrices at pG+0xA4 (view) / pG+0xD8 (projection),
// depth test against the game's buffer, one state block captured/applied.
#include "worldmodel.h"
#include "worldmachines.h"
#include "game.h"
#include "log.h"

#include <windows.h>
#include <d3d9.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Use the project's rebased, validated pointers (bio4.exe loads at a random base).
constexpr DWORD kFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct FileVtx { float p[3], n[3], uv[2]; uint32_t c; };
struct Vtx { float x, y, z, nx, ny, nz; DWORD c; float u, v; };
struct Sub { uint32_t first, count; int32_t tex; uint32_t flags; };

struct Model {
    wchar_t file[64] = {};
    bool tried = false, ok = false;
    FILETIME stamp{};
    DWORD checkedMs = 0;
    std::vector<Vtx> verts;
    std::vector<uint32_t> idx;
    std::vector<Sub> subs;
    struct Tex { uint32_t w = 0, h = 0; std::vector<uint32_t> bgra; IDirect3DTexture9* t = nullptr; };
    std::vector<Tex> tex;
    float lo[3] = {}, hi[3] = {};
    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    IDirect3DDevice9* dev = nullptr;
};
Model g_models[8];
IDirect3DStateBlock9* g_sb = nullptr;
IDirect3DDevice9* g_sbDev = nullptr;

uint8_t* Global() { return reinterpret_cast<uint8_t*>(GetGame()); }

void PathOf(const Model& m, wchar_t* path) {
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\')) *slash = 0;
    swprintf_s(path + wcslen(path), MAX_PATH - wcslen(path), L"\\re4z\\models\\%s", m.file);
}

void Release(Model& m) {
    for (auto& t : m.tex) if (t.t) { t.t->Release(); t.t = nullptr; }
    if (m.vb) { m.vb->Release(); m.vb = nullptr; }
    if (m.ib) { m.ib->Release(); m.ib = nullptr; }
    m.verts.clear(); m.idx.clear(); m.subs.clear(); m.tex.clear();
    m.dev = nullptr; m.ok = false;
}

bool ReadFile(Model& m) {
    wchar_t path[MAX_PATH];
    PathOf(m, path);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") || !f) return false;
    struct Head { char magic[4]; uint32_t version, nv, ni, ns, nt; float lo[3], hi[3]; } h{};
    bool good = fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "R4M1", 4) &&
                h.version == 2 && h.nv && h.nv < 2000000 && h.ni < 6000000 &&
                h.ns < 4096 && h.nt < 64;
    if (good) {
        std::vector<FileVtx> fv(h.nv);
        m.idx.resize(h.ni);
        m.subs.resize(h.ns);
        good = fread(fv.data(), sizeof(FileVtx), h.nv, f) == h.nv &&
               fread(m.idx.data(), 4, h.ni, f) == h.ni &&
               fread(m.subs.data(), sizeof(Sub), h.ns, f) == h.ns;
        m.verts.resize(h.nv);
        for (uint32_t i = 0; good && i < h.nv; ++i) {
            const FileVtx& a = fv[i];
            m.verts[i] = { a.p[0], a.p[1], a.p[2], a.n[0], a.n[1], a.n[2], a.c, a.uv[0], a.uv[1] };
        }
        for (uint32_t i = 0; good && i < h.ni; ++i) good = m.idx[i] < h.nv;
        for (const Sub& s : m.subs) good = good && s.first + s.count <= h.ni && s.tex < (int32_t)h.nt;
        m.tex.resize(h.nt);
        for (uint32_t t = 0; good && t < h.nt; ++t) {
            uint32_t wh[2];
            good = fread(wh, 4, 2, f) == 2 && wh[0] && wh[1] && wh[0] <= 4096 && wh[1] <= 4096;
            if (!good) break;
            m.tex[t].w = wh[0]; m.tex[t].h = wh[1];
            m.tex[t].bgra.resize((size_t)wh[0] * wh[1]);
            good = fread(m.tex[t].bgra.data(), 4, m.tex[t].bgra.size(), f) == m.tex[t].bgra.size();
        }
        memcpy(m.lo, h.lo, sizeof m.lo);
        memcpy(m.hi, h.hi, sizeof m.hi);
    }
    fclose(f);
    if (!good) Log("worldmodel: %ls is not a version-2 .r4m", path);
    return good;
}

bool MakeBuffers(IDirect3DDevice9* dev, Model& m) {
    if (m.vb && m.dev == dev) return true;
    if (FAILED(dev->CreateVertexBuffer((UINT)(m.verts.size() * sizeof(Vtx)), D3DUSAGE_WRITEONLY, kFvf,
                                        D3DPOOL_MANAGED, &m.vb, nullptr))) return false;
    void* p = nullptr;
    if (FAILED(m.vb->Lock(0, 0, &p, 0))) return false;
    memcpy(p, m.verts.data(), m.verts.size() * sizeof(Vtx));
    m.vb->Unlock();
    if (FAILED(dev->CreateIndexBuffer((UINT)(m.idx.size() * 4), D3DUSAGE_WRITEONLY, D3DFMT_INDEX32,
                                       D3DPOOL_MANAGED, &m.ib, nullptr))) return false;
    if (FAILED(m.ib->Lock(0, 0, &p, 0))) return false;
    memcpy(p, m.idx.data(), m.idx.size() * 4);
    m.ib->Unlock();
    for (auto& t : m.tex) {
        if (FAILED(dev->CreateTexture(t.w, t.h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t.t, nullptr))) { t.t = nullptr; continue; }
        D3DLOCKED_RECT lr{};
        if (SUCCEEDED(t.t->LockRect(0, &lr, nullptr, 0))) {
            for (uint32_t y = 0; y < t.h; ++y)
                memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch, t.bgra.data() + (size_t)y * t.w, t.w * 4);
            t.t->UnlockRect(0);
        }
    }
    m.dev = dev;
    return true;
}

Model* Get(const wchar_t* file) {
    Model* freeSlot = nullptr;
    for (Model& m : g_models) {
        if (!wcscmp(m.file, file)) return &m;
        if (!m.file[0] && !freeSlot) freeSlot = &m;
    }
    if (!freeSlot) return nullptr;
    wcsncpy_s(freeSlot->file, file, _TRUNCATE);
    return freeSlot;
}

bool EnsureLoaded(const wchar_t* file) {
    Model* m = Get(file);
    if (!m) return false;
    if (!m->tried || GetTickCount() - m->checkedMs > 1000) {
        m->checkedMs = GetTickCount();
        wchar_t path[MAX_PATH];
        PathOf(*m, path);
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        bool there = GetFileAttributesExW(path, GetFileExInfoStandard, &fa) != 0;
        if (!m->tried && !there) { m->tried = true; Log("worldmodel: no %ls (watching)", path); }
        if (there && (!m->tried || CompareFileTime(&fa.ftLastWriteTime, &m->stamp) != 0)) {
            Release(*m);
            m->tried = true;
            m->stamp = fa.ftLastWriteTime;
            m->ok = ReadFile(*m);
            if (m->ok) Log("worldmodel: %ls: %zu verts, %zu tris, %zu parts, %zu textures",
                          file, m->verts.size(), m->idx.size() / 3, m->subs.size(), m->tex.size());
        }
    }
    return m->ok;
}

// Camera matrices from the game's render camera (pG+0xA4 view GX 3x4, pG+0xD8 proj GX 44),
// ported verbatim from boshjerns own_model.cpp CameraMatrices().
bool CameraMatrices(const uint8_t* g, float bbAspect, D3DMATRIX& view, D3DMATRIX& proj) {
    if (!g) return false;
    float V[12], P[16];
    memcpy(V, g + 0xa4, sizeof V);
    memcpy(P, g + 0xd8, sizeof P);
    for (float f : V) if (!std::isfinite(f)) return false;
    for (float f : P) if (!std::isfinite(f)) return false;
    if (!(P[5] > 0.1f) || !(P[0] > 0.1f) || fabsf(P[14] + 1.f) > 0.01f) return false;
    // Diagnostic toggle: F9 switches the unconfirmed GX aspect correction off.
    // Default is boshjerns' Hor+ rescale. If the machines stay glued to the world
    // with F9 on, the game uses the GX projection unscaled instead.
    {
        static bool noAspect = false, prev = false;
        const bool down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (down && !prev) noAspect = !noAspect;
        prev = down;
        if (noAspect) bbAspect = P[5] / P[0]; // F9: keep the game's GX aspect
    }
    const float v[16] = { V[0], V[4], V[8], 0.f, V[1], V[5], V[9], 0.f,
                          V[2], V[6], V[10], 0.f, V[3], V[7], V[11], 1.f };
    memcpy(&view, v, sizeof v);
    const float k = (P[5] / P[0]) / bbAspect;
    float r0[4] = { P[0]*k, P[1]*k, P[2]*k, P[3]*k }, r1[4] = { P[4], P[5], P[6], P[7] };
    float r2[4] = { P[8]+P[12], P[9]+P[13], P[10]+P[14], P[11]+P[15] }, r3[4] = { P[12], P[13], P[14], P[15] };
    const float pm[16] = { r0[0], r1[0], r2[0], r3[0], r0[1], r1[1], r2[1], r3[1],
                           r0[2], r1[2], r2[2], r3[2], r0[3], r1[3], r2[3], r3[3] };
    memcpy(&proj, pm, sizeof pm);
    return true;
}

void SetStates(IDirect3DDevice9* dev, UINT w, UINT h, bool depth, const D3DMATRIX& world,
               const D3DMATRIX& view, const D3DMATRIX& proj) {
    D3DVIEWPORT9 vp{ 0, 0, w, h, 0.f, 1.f };
    dev->SetViewport(&vp);
    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetFVF(kFvf);
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetRenderState(D3DRS_AMBIENT, 0xFF5A5A5A);
    dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, depth ? D3DZB_TRUE : D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, depth ? TRUE : FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHAREF, 0x80);
    dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPING, TRUE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    D3DLIGHT9 light{};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse = { 0.85f, 0.85f, 0.85f, 1.f };
    const float lx = 0.25f, ly = -0.8f, lz = 0.55f;
    light.Direction = { view._11*lx + view._12*ly + view._13*lz,
                        view._21*lx + view._22*ly + view._23*lz,
                        view._31*lx + view._32*ly + view._33*lz };
    dev->SetLight(0, &light);
    dev->LightEnable(0, TRUE);
    D3DMATERIAL9 mat{};
    mat.Diffuse = mat.Ambient = { 1.f, 1.f, 1.f, 1.f };
    dev->SetMaterial(&mat);
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetStreamSource(0, nullptr, 0, 0);
    dev->SetIndices(nullptr);
}

D3DMATRIX BuildWorld(float wx, float wy, float wz, float yaw, float scale,
                     const float lo[3], const float hi[3], bool zUp) {
    // D3D9 row vectors: v' = v * M. Compose M = Ry * fix: first bring the model
    // upright (Z-up -> Y-up), then rotate it to its fixed facing in the world.
    // Ry: yaw around world Y. fix: -90deg about X for Z-up models (old Z -> up).
    const float c = cosf(yaw), s = sinf(yaw);
    float ry[4][4] = { {c,0,-s,0}, {0,1,0,0}, {s,0,c,0}, {0,0,0,1} };
    float fx[4][4] = {
        {1,0,0,0},
        {0,0,-1,0},   // -90deg X: old Z -> +Y
        {0,1,0,0},
        {0,0,0,1}
    };
    if (!zUp) { fx[0][0]=fx[1][1]=fx[2][2]=fx[3][3]=1; fx[1][2]=fx[2][1]=0; }

    float R[4][4] = {};
    for (int r = 0; r < 4; ++r)
        for (int col = 0; col < 4; ++col)
            for (int k = 0; k < 4; ++k)
                R[r][col] += fx[r][k] * ry[k][col];

    D3DMATRIX m{};
    float* out = &m._11;
    for (int r = 0; r < 4; ++r)
        for (int col = 0; col < 4; ++col)
            out[r*4+col] = R[r][col] * ((col < 3) ? scale : 1.f);

    // Sit the bbox bottom on the ground: after fix, the model's up axis (old Z
    // for zUp, old Y otherwise) becomes world Y. Translate by -bottom*scale.
    const float bottom = zUp ? lo[2] : lo[1];
    m._41 = wx; m._42 = wy - bottom * scale; m._43 = wz;
    return m;
}

} // namespace

static void DrawLoaded(IDirect3DDevice9* device, Model& m, const D3DMATRIX& world) {
    if (!MakeBuffers(device, m)) return;
    uint8_t* g = Global();
    IDirect3DSurface9* bb = nullptr;
    if (!g || FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC bd{};
    bb->GetDesc(&bd);
    // Use the GX-derived camera matrices (proven to work in this engine).
    D3DMATRIX view{}, proj{};
    if (!bd.Height || !CameraMatrices(g, (float)bd.Width / bd.Height, view, proj)) { bb->Release(); return; }

    // One-shot camera dump + manual D3D-vs-GX toggle (F10).
    // F9 (handled inside CameraMatrices) toggles the GX aspect correction.
    // F10 toggles use of the game's actual D3D view/projection matrices instead
    // of the GX-derived ones: those are what the game rendered the scene with,
    // so they should eliminate any drift. Falls back to GX if they look invalid.
    static bool useD3D = false, prevF10 = false;
    const bool f10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (f10 && !prevF10) useD3D = !useD3D;
    prevF10 = f10;
    {
        static bool logged = false;
        if (!logged) {
            logged = true;
            D3DMATRIX dv{}, dp{};
            device->GetTransform(D3DTS_VIEW, &dv);
            device->GetTransform(D3DTS_PROJECTION, &dp);
            Log("cam GX: view t=(%.1f %.1f %.1f) proj=(%.4f %.4f %.4f %.4f) aspect=%.3f",
                view._41, view._42, view._43, proj._11, proj._22, proj._34, proj._44,
                (float)bd.Width / bd.Height);
            Log("cam D3D: view t=(%.1f %.1f %.1f) proj=(%.4f %.4f %.4f %.4f)",
                dv._41, dv._42, dv._43, dp._11, dp._22, dp._34, dp._44);
            Log("cam: F9 toggles GX aspect, F10 toggles D3D transforms (currently %s)",
                useD3D ? "D3D" : "GX");
        }
    }
    if (useD3D) {
        D3DMATRIX dv{}, dp{};
        device->GetTransform(D3DTS_VIEW, &dv);
        device->GetTransform(D3DTS_PROJECTION, &dp);
        // Basic validity: a real perspective projection has _34 near +-1 and _44 near 0.
        const bool valid = (fabsf(dp._34) > 0.5f) && (fabsf(dp._44) < 0.01f) &&
                           (fabsf(dv._41) + fabsf(dv._42) + fabsf(dv._43) > 1.0f);
        if (valid) { view = dv; proj = dp; }
        else Log("cam: D3D transforms invalid, staying on GX");
    }

    if (g_sb && g_sbDev != device) { g_sb->Release(); g_sb = nullptr; }
    if (!g_sb) {
        if (FAILED(device->BeginStateBlock())) { bb->Release(); return; }
        SetStates(device, bd.Width, bd.Height, true, world, view, proj);
        if (FAILED(device->EndStateBlock(&g_sb)) || !g_sb) { g_sb = nullptr; bb->Release(); return; }
        g_sbDev = device;
    }
    g_sb->Capture();

    IDirect3DSurface9* prevRt = nullptr;
    IDirect3DSurface9* ds = nullptr;
    device->GetRenderTarget(0, &prevRt);
    device->GetDepthStencilSurface(&ds);
    bool depth = false;
    if (ds) {
        D3DSURFACE_DESC dd{};
        ds->GetDesc(&dd);
        depth = dd.Width >= bd.Width && dd.Height >= bd.Height && dd.MultiSampleType == bd.MultiSampleType;
    }
    device->SetRenderTarget(0, bb);
    if (!depth && ds) device->SetDepthStencilSurface(nullptr);
    SetStates(device, bd.Width, bd.Height, depth, world, view, proj);

    // No BeginScene/EndScene here: we are called from inside our overlay's scene.
    for (const Sub& s : m.subs) {
        if (!s.count) continue;
        IDirect3DTexture9* t = s.tex >= 0 ? m.tex[s.tex].t : nullptr;
        device->SetTexture(0, t);
        device->SetTextureStageState(0, D3DTSS_COLOROP, t ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, t ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
        device->SetRenderState(D3DRS_LIGHTING, (s.flags & 1) ? FALSE : TRUE);
        device->SetStreamSource(0, m.vb, 0, sizeof(Vtx));
        device->SetIndices(m.ib);
        device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, (UINT)m.verts.size(), s.first, s.count / 3);
    }
    device->SetStreamSource(0, nullptr, 0, 0);
    device->SetIndices(nullptr);
    device->SetTexture(0, nullptr);

    device->SetRenderTarget(0, prevRt);
    if (!depth && ds) device->SetDepthStencilSurface(ds);
    g_sb->Apply();
    if (prevRt) prevRt->Release();
    if (ds) ds->Release();
    bb->Release();
}

// v' = v * M (D3D9 row vectors). out = a * b means: apply a first, then b.
static void Mul(D3DMATRIX& out, const D3DMATRIX& a, const D3DMATRIX& b) {
    const float* pa = &a._11; const float* pb = &b._11; float* po = &out._11;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += pa[r * 4 + k] * pb[k * 4 + c];
            po[r * 4 + c] = s;
        }
}
static D3DMATRIX Trans(float x, float y, float z) {
    D3DMATRIX m{}; m._11 = m._22 = m._33 = m._44 = 1.f;
    m._41 = x; m._42 = y; m._43 = z; return m;
}
static D3DMATRIX RotX(float a) {
    D3DMATRIX m{}; m._44 = 1.f;
    m._11 = 1.f; m._22 = cosf(a); m._23 = sinf(a); m._32 = -sinf(a); m._33 = cosf(a);
    return m;
}
static D3DMATRIX RotY(float a) {
    D3DMATRIX m{}; m._44 = 1.f;
    m._11 = cosf(a); m._13 = -sinf(a); m._22 = 1.f; m._31 = sinf(a); m._33 = cosf(a);
    return m;
}
static D3DMATRIX Scale(float s) {
    D3DMATRIX m{}; m._11 = m._22 = m._33 = s; m._44 = 1.f; return m;
}

void WorldModel_DrawMatrix(IDirect3DDevice9* device, const wchar_t* file,
                           const D3DMATRIX& world) {
    if (!device || !EnsureLoaded(file)) return;
    DrawLoaded(device, *Get(file), world);
}

void WorldModel_DrawCentered(IDirect3DDevice9* device, const wchar_t* file,
                             float wx, float wy, float wz, float yaw, float scale,
                             bool zUp) {
    if (!device || !EnsureLoaded(file)) return;
    Model& m = *Get(file);
    const float cx = (m.lo[0] + m.hi[0]) * 0.5f;
    const float cy = (m.lo[1] + m.hi[1]) * 0.5f;
    const float cz = (m.lo[2] + m.hi[2]) * 0.5f;
    D3DMATRIX w = Trans(-cx, -cy, -cz);
    D3DMATRIX t{};
    if (zUp) { Mul(t, w, RotX(-1.5707963f)); w = t; }
    Mul(t, w, RotY(yaw));
    Mul(w, t, Scale(scale));
    Mul(t, w, Trans(wx, wy, wz));
    DrawLoaded(device, m, t);
}

void WorldModel_DrawHinged(IDirect3DDevice9* device, const wchar_t* file,
                           float wx, float wy, float wz, float yaw,
                           float hingeY, float pitch, float scale, bool zUp) {
    if (!device || !EnsureLoaded(file)) return;
    Model& m = *Get(file);
    D3DMATRIX w = Trans(0.f, -hingeY, 0.f);
    D3DMATRIX t{};
    Mul(t, w, RotX(pitch));           // swing about the local X hinge
    Mul(w, t, Trans(0.f, hingeY, 0.f));
    if (zUp) {                        // BO1 models are Z-up: old Z -> world +Y
        Mul(t, w, RotX(-1.5707963f));
        w = t;
    }
    Mul(t, w, RotY(yaw));
    Mul(w, t, Scale(scale));
    Mul(t, w, Trans(wx, wy, wz));
    DrawLoaded(device, m, t);
}

void WorldModel_DrawAt(IDirect3DDevice9* device, const wchar_t* file,
                       float wx, float wy, float wz, float yaw, float scale, bool zUp) {
    if (!device || !EnsureLoaded(file)) return;
    Model& m = *Get(file);
    DrawLoaded(device, m, BuildWorld(wx, wy, wz, yaw, scale, m.lo, m.hi, zUp));
}

// World anchor: captured once per room from the player's spawn, then machines
// stay put (no camera follow). Re-anchored when the room id changes.
// Machines use the single fixed anchor from worldmachines.cpp. Nothing follows
// the camera: every machine is always at kAnchor + its fixed offset.
static float g_anchorPos[3] = { 0, 0, 0 };

void WorldModel_DrawAtSpawn(IDirect3DDevice9* device, const wchar_t* file,
                            float offX, float offY, float offZ, float yaw, float scale, bool zUp) {
    WorldMachines_Anchor(g_anchorPos);
    WorldModel_DrawAt(device, file,
                      g_anchorPos[0] + offX, g_anchorPos[1] + offY, g_anchorPos[2] + offZ,
                      yaw, scale, zUp);
}

// Draw a chalk-white weapon sprite as a world-space billboard (wall buys).
void WorldModel_DrawBillboard(IDirect3DDevice9* device, IDirect3DTexture9* tex,
                              float wx, float wy, float wz, float yaw, float w, float h) {
    if (!device || !tex) return;
    uint8_t* g = Global();
    if (!g) return;
    D3DMATRIX view{}, proj{};
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC bd{}; bb->GetDesc(&bd);
    if (!bd.Height || !CameraMatrices(g, (float)bd.Width / bd.Height, view, proj)) { bb->Release(); return; }

    // Project the four billboard corners through view*proj into screen space.
    const float c = cosf(yaw), s = sinf(yaw);
    // Build billboard right/up vectors in world space (yaw around Y, face camera).
    float right[3] = { c, 0.f, -s };
    float up[3] = { 0.f, 1.f, 0.f };
    struct V { float x,y,z,rhw; D3DCOLOR col; float u,v; };
    V quad[4];
    const float uvs[4][2] = { {0,0},{1,0},{1,1},{0,1} };
    const float sgn[4][2] = { {-1,1},{1,1},{1,-1},{-1,-1} };
    for (int i = 0; i < 4; ++i) {
        float px = wx + right[0]*sgn[i][0]*w*0.5f + up[0]*sgn[i][1]*h*0.5f;
        float py = wy + right[1]*sgn[i][0]*w*0.5f + up[1]*sgn[i][1]*h*0.5f;
        float pz = wz + right[2]*sgn[i][0]*w*0.5f + up[2]*sgn[i][1]*h*0.5f;
        // v * view (D3D row vectors)
        float vx = px*view._11 + py*view._21 + pz*view._31 + view._41;
        float vy = px*view._12 + py*view._22 + pz*view._32 + view._42;
        float vz = px*view._13 + py*view._23 + pz*view._33 + view._43;
        // v * proj
        float cx = vx*proj._11 + vy*proj._21 + vz*proj._31 + proj._41;
        float cy = vx*proj._12 + vy*proj._22 + vz*proj._32 + proj._42;
        float cw = vx*proj._14 + vy*proj._24 + vz*proj._34 + proj._44;
        if (cw < 0.001f) cw = 0.001f;
        quad[i].x = bd.Width*0.5f + cx/cw*bd.Width*0.5f;
        quad[i].y = bd.Height*0.5f - cy/cw*bd.Height*0.5f;
        quad[i].z = 0.f; quad[i].rhw = 1.f;
        quad[i].col = D3DCOLOR_ARGB(220, 236, 232, 220); // chalk white
        quad[i].u = uvs[i][0]; quad[i].v = uvs[i][1];
    }
    // Draw in screen space with depth off (we're inside the 3D scene but using RHW quads).
    device->SetVertexShader(nullptr); device->SetPixelShader(nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetTexture(0, tex);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, quad, sizeof(V));
    device->SetTexture(0, nullptr);
    bb->Release();
}

void WorldModel_Reset() {
    if (g_sb) { g_sb->Release(); g_sb = nullptr; }
    g_sbDev = nullptr;
}
