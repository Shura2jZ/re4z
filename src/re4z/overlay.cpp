#include "overlay.h"
#include "game.h"
#include "log.h"
#include "zombies.h"
#include "hud_assets.h"
#include "worldmodel.h"
#include "worldmachines.h"

#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <cmath>
#include "bo1font.h"

namespace {

constexpr float LINE_H = 27.0f;

struct TextVertex {
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v;
    uint8_t atlas;   // 0 = gamefonts_pc (retail), 1 = devfonts
};

struct RectVertex {
    float x, y, z, rhw;
    D3DCOLOR color;
};

IDirect3DTexture9* g_fontTexture = nullptr;
IDirect3DTexture9* g_devFontTexture = nullptr;
IDirect3DTexture9* g_crosshairTex = nullptr;
IDirect3DTexture9* g_weaponIconTex = nullptr;
IDirect3DTexture9* g_scorebarTex = nullptr;
IDirect3DTexture9* g_ammoCounterTex = nullptr;
int g_ammoCounterId = 0;
IDirect3DTexture9* g_perkTex[4] = { nullptr, nullptr, nullptr, nullptr };
IDirect3DTexture9* g_boxWeaponTex[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
// Real BO1 wall-buy chalk drawings (black background made transparent in the DDS alpha).
IDirect3DTexture9* g_chalkTex[3] = { nullptr, nullptr, nullptr };
IDirect3DTexture9* g_lowHealthTex = nullptr;
IDirect3DTexture9* g_compassHealthTex = nullptr;
bool g_perkActive[4] = { false, false, false, false };
const char* g_perkNames[4] = {
    "hud/zombie/specialty_juggernaut_zombies.dds",
    "hud/zombie/specialty_quickrevive_zombies.dds",
    "hud/zombie/specialty_fastreload_zombies.dds",
    "hud/zombie/specialty_doubletap_zombies.dds",
};
int g_weaponIconId = 0;
bool g_showDebug = false;
float g_fps = 0.0f;
DWORD g_lastFpsTick = 0;
int g_fpsFrames = 0;
bool g_beginSceneErrorLogged = false;

void AddRect(std::vector<RectVertex>& out, float x, float y, float w, float h, D3DCOLOR color) {
    RectVertex a = { x,     y,     0.0f, 1.0f, color };
    RectVertex b = { x + w, y,     0.0f, 1.0f, color };
    RectVertex c = { x + w, y + h, 0.0f, 1.0f, color };
    RectVertex d = { x,     y + h, 0.0f, 1.0f, color };
    out.push_back(a); out.push_back(b); out.push_back(c);
    out.push_back(a); out.push_back(c); out.push_back(d);
}

static const char* AmmoCounterName(int itemId) {
    switch (itemId) {
    case 0x25: case 0x26: case 0x27: case 0x28: return "ammo_counter_shotgunshell";
    case 0x29: case 0x2A: case 0x2C: case 0x21: case 0x22: return "ammo_counter_riflebullet";
    case 0x34: return "ammo_counter_rocket";
    default: return "ammo_counter_bullet";
    }
}

static const char* WeaponIconName(int itemId) {
    switch (itemId) {
    case 0x23: return "hud_colt";                  // M1911
    case 0x24: return "hud_cz75";                  // Blacktail -> CZ75
    case 0x21: return "hud_colt";                  // Red9
    case 0x22: return "hud_cz75";                  // Punisher -> CZ75
    case 0x25: case 0x26: return "hud_spas12";     // M870 / Striker (shotgun)
    case 0x27: case 0x28: return "hud_spas12";     // shotgun variants
    case 0x29: return "hud_m14";                   // Rifle (Semi-Auto)
    case 0x2A: return "hud_m14";                   // Rifle (Bolt-Action)
    case 0x2C: return "hud_m14";                   // Chicago Typewriter
    case 0x2E: case 0x2F: case 0x51: case 0x6B: case 0x6C: case 0x99: return "hud_uzi";  // TMP variants
    case 0x30: case 0x31: case 0x32: case 0x33: return "hud_cz75";  // Broken Butterfly / Killer7 / Magnum
    case 0x34: return "hud_rpg";                   // Rocket Launcher
    case 0x35: return "hud_colt";                  // Mine Thrower (no icon, fallback)
    default: return "hud_colt";
    }
}

// Draws text with a real BO1 font atlas + exact glyph metrics dumped from the
// fastfiles. `y` is the pen origin (baseline). `xs` optionally condenses the
// glyphs horizontally (used by the tall condensed round counter).
// Builds one glyph quad. `inflate` grows the quad around its centre, which is
// how BO1's glow font material produces its soft halo.
void AddTextX(std::vector<TextVertex>& out, float x, float y, const char* text,
              D3DCOLOR color, const Bo1Font& font, float scale, float xs,
              float inflate = 0.0f, float inflateAlpha = 1.0f) {
    const float startX = x;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        unsigned char c = *p;
        if (c == '\n') {
            x = startX;
            y += font.pixelHeight * scale;
            continue;
        }
        if (c < 32 || c > 126) c = '?';
        const Bo1FontGlyph& g = font.glyphs[c - 32];
        if (g.w > 0.0f && g.h > 0.0f) {
            const float gx = x + g.x0 * scale * xs;
            const float gy = y + g.y0 * scale;
            const float gw = g.w * scale * xs;
            const float gh = g.h * scale;
            const uint8_t atlas = (font.glyphs == kBo1FontDevBig ||
                                   font.glyphs == kBo1FontDevSmall) ? 1 : 0;
            const D3DCOLOR col = (inflateAlpha < 1.0f)
                ? ((D3DCOLOR)((((DWORD)(BYTE)(255.0f * inflateAlpha)) << 24) |
                                (color & 0x00FFFFFF)))
                : color;
            TextVertex a = { gx,     gy,     0.0f, 1.0f, col, g.u0, g.v0, atlas };
            TextVertex b = { gx + gw, gy,     0.0f, 1.0f, col, g.u1, g.v0, atlas };
            TextVertex d = { gx + gw, gy + gh, 0.0f, 1.0f, col, g.u1, g.v1, atlas };
            TextVertex e = { gx,     gy + gh, 0.0f, 1.0f, col, g.u0, g.v1, atlas };
            if (inflate > 0.0f) {
                // Grow the quad symmetrically around its centre.
                const float cx = gx + gw * 0.5f;
                const float cy = gy + gh * 0.5f;
                const float nw = gw + inflate * 2.0f;
                const float nh = gh + inflate * 2.0f;
                a.x = cx - nw * 0.5f; a.y = cy - nh * 0.5f;
                b.x = cx + nw * 0.5f; b.y = cy - nh * 0.5f;
                d.x = cx + nw * 0.5f; d.y = cy + nh * 0.5f;
                e.x = cx - nw * 0.5f; e.y = cy + nh * 0.5f;
            }
            out.push_back(a); out.push_back(b); out.push_back(d);
            out.push_back(a); out.push_back(d); out.push_back(e);
        }
        x += g.advance * scale * xs;
    }
}

void AddText(std::vector<TextVertex>& out, float x, float y, const char* text,
             D3DCOLOR color, const Bo1Font& font, float scale) {
    AddTextX(out, x, y, text, color, font, scale, 1.0f);
}

float TextWidthX(const char* text, const Bo1Font& font, float scale, float xs) {
    float w = 0.0f;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        unsigned char c = *p;
        if (c < 32 || c > 126) c = '?';
        w += font.glyphs[c - 32].advance * scale * xs;
    }
    return w;
}

// BO1's HUD fonts are drawn condensed: each glyph advances by a fixed number of
// screen pixels and digits have a fixed cap height, independent of the font's
// native metrics. This helper reproduces that.
void AddTextCondensed(std::vector<TextVertex>& out, float x, float y, const char* text,
                      D3DCOLOR color, const Bo1Font& font, float capPx, float advancePx,
                      float inflate = 0.0f, float inflateAlpha = 1.0f) {
    const Bo1FontGlyph& z = font.glyphs['0' - 32];
    const float scale = (z.h > 0.0f) ? capPx / z.h : 1.0f;
    const float xs = (z.advance > 0.0f) ? advancePx / (z.advance * scale) : 1.0f;
    AddTextX(out, x, y, text, color, font, scale, xs, inflate, inflateAlpha);
}

// Right-aligned condensed text: `x` is the right edge of the last glyph.
void AddTextCondensedRight(std::vector<TextVertex>& out, float x, float y, const char* text,
                           D3DCOLOR color, const Bo1Font& font, float capPx, float advancePx) {
    const Bo1FontGlyph& z = font.glyphs['0' - 32];
    const float scale = (z.h > 0.0f) ? capPx / z.h : 1.0f;
    const float xs = (z.advance > 0.0f) ? advancePx / (z.advance * scale) : 1.0f;
    AddTextX(out, x - TextWidthX(text, font, scale, xs), y, text, color, font, scale, xs);
}

bool CreateFontAtlas(IDirect3DDevice9* device) {
    // Two real BO1 font atlases:
    //   fonts/gamefonts_pc.dds  (512x1024) retail fonts
    //   fonts/devfonts_bo1.dds  (256x256)  dev fonts (round counter)
    // Glyph metrics live in bo1font.h.
    char path[MAX_PATH] = {};
    if (!BuildAssetPath(path, sizeof(path), "fonts/gamefonts_pc.dds")) {
        Log("overlay: cannot build font atlas path");
        return false;
    }
    g_fontTexture = LoadDdsTexture(device, path);
    if (!g_fontTexture) {
        Log("overlay: BO1 retail font atlas load failed");
        return false;
    }
    char devPath[MAX_PATH] = {};
    if (BuildAssetPath(devPath, sizeof(devPath), "fonts/devfonts_bo1.dds")) {
        g_devFontTexture = LoadDdsTexture(device, devPath);
    }
    Log("overlay: BO1 font atlases loaded (retail=%p dev=%p)", g_fontTexture, g_devFontTexture);
    return true;
}

// Draws a soft additive halo around text, mimicking BO1's "glow" font material
// (fonts/gamefonts_pc_glow). The halo is built from several dilated copies of
// the glyph quads blended additively, which reproduces the diffused look of
// the round counter in the game.
//
// The blend must be SRCALPHA/ONE: with ONE/ONE the whole quad is added and the
// glow turns into a solid rectangle instead of following the glyph.
void DrawTextGlow(IDirect3DDevice9* device, std::vector<TextVertex>& text) {
    if (text.empty() || !g_fontTexture) return;
    device->SetTexture(0, g_fontTexture);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    // Additive, but weighted by the source alpha so the halo keeps the glyph
    // shape instead of filling the whole quad.
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(text.size() / 3),
                            text.data(), sizeof(TextVertex));
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetTexture(0, nullptr);
}

void DrawRectBatch(IDirect3DDevice9* device, std::vector<RectVertex>& rects) {
    if (rects.empty()) return;
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(rects.size() / 3),
                            rects.data(), sizeof(RectVertex));
}

// Draws one textured quad with the standard modulate state (perk badges, the
// scorebar splat). Alpha-blended: the source textures carry their shape in
// alpha, so blending must be on regardless of the game's current state.
static void DrawSprite(IDirect3DDevice9* device, IDirect3DTexture9* tex,
                       float x, float y, float w, float h, D3DCOLOR tint) {
    if (!tex) return;
    struct TQV { float x, y, z, rhw; D3DCOLOR c; float u, v; };
    TQV q[4] = {
        { x,     y,     0.0f, 1.0f, tint, 0.0f, 0.0f },
        { x + w, y,     0.0f, 1.0f, tint, 1.0f, 0.0f },
        { x + w, y + h, 0.0f, 1.0f, tint, 1.0f, 1.0f },
        { x,     y + h, 0.0f, 1.0f, tint, 0.0f, 1.0f },
    };
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetTexture(0, tex);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, q, sizeof(TQV));
    device->SetTexture(0, nullptr);
}

void DrawTextBatch(IDirect3DDevice9* device, std::vector<TextVertex>& text) {
    if (text.empty()) return;
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

    // Two atlases: retail (gamefonts_pc) and dev (devfonts). Draw each group.
    for (int pass = 0; pass < 2; ++pass) {
        IDirect3DTexture9* tex = (pass == 0) ? g_fontTexture : g_devFontTexture;
        if (!tex) continue;
        std::vector<TextVertex> batch;
        batch.reserve(text.size());
        for (const TextVertex& v : text) {
            if (v.atlas == pass) batch.push_back(v);
        }
        if (batch.empty()) continue;
        device->SetTexture(0, tex);
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(batch.size() / 3),
                                batch.data(), sizeof(TextVertex));
    }
    device->SetTexture(0, nullptr);
}

} // namespace

void Overlay_OnReset() {
    WorldModel_Reset();
    if (g_fontTexture) {
        g_fontTexture->Release();
        g_fontTexture = nullptr;
    }
    if (g_crosshairTex) {
        g_crosshairTex->Release();
        g_crosshairTex = nullptr;
    }
    for (IDirect3DTexture9*& t : g_chalkTex) {
        if (t) { t->Release(); t = nullptr; }
    }
}

// Export used by the MW2RE4 host's Present hook: the host owns device vtable
// slot 17, so it calls us to draw the BO1 HUD each frame.
extern "C" __declspec(dllexport) void __cdecl BO1Z_RenderOverlay(IDirect3DDevice9* device) {
    Overlay_Render(device);
}

void Overlay_DrawWorldModels(IDirect3DDevice9* device) {
    const ZombiesHud& z = Zombies_Get();
    if (!z.valid || !z.inGame) return;
    const WorldMachine* machines = WorldMachines();
    for (int i = 0; i < WM_COUNT; ++i) {
        const WorldMachine& m = machines[i];
        WorldModel_DrawAtSpawn(device, m.file, m.offX, m.offY, m.offZ, m.yaw, m.scale, m.zUp);
    }
    // Mystery Box animation: hinged lid + a real BO1 weapon model that spins
    // above the open box, then stays until the player presses F to take it.
    if (z.boxSpinning || z.boxWaiting) {
        float anch[3]; WorldMachines_Anchor(anch);
        const WorldMachine& box = machines[0];
        const float bx = anch[0] + box.offX;
        const float by = anch[1] + box.offY;
        const float bz = anch[2] + box.offZ;
        const float spin = z.boxSpin;
        // Lid: opens during the first 15% of the spin, stays open while waiting.
        const float open = z.boxWaiting ? 1.0f : (spin < 0.15f ? spin / 0.15f : 1.0f);
        // Z-up lid: the painted "??" face is on top when closed; the hinge is
        // the local Y edge. Swing about X to lift it ~110 degrees.
        const float pitch = 1.92f * open;
        WorldModel_DrawHinged(device, L"boxlid.r4m", bx, by + 445.0f, bz,
                              0.0f, -0.859f, pitch, box.scale, true);
        // Weapon: cycles quickly during the spin, then holds on the result.
        static const wchar_t* kGuns[16] = {
            L"boxgun_1911.r4m", L"boxgun_mp5.r4m", L"boxgun_ak74u.r4m",
            L"boxgun_m14.r4m", L"boxgun_ithaca.r4m", L"boxgun_python.r4m",
            L"boxgun_raygun.r4m", L"boxgun_thundergun.r4m", L"boxgun_pm63.r4m",
            L"boxgun_spas.r4m", L"boxgun_hk21.r4m", L"boxgun_l96.r4m",
            L"boxgun_dragunov.r4m", L"boxgun_ak47.r4m",
            L"boxgun_tesla.r4m", L"boxgun_tesla_up.r4m"
        };
        const int idx = z.boxWaiting ? z.boxResult
                                     : ((int)(spin * 80.0f) % 16);
        if (idx >= 0 && idx < 16) {
            const float yaw = (float)(GetTickCount() % 3600) / 3600.0f * 6.283185f;
            WorldModel_DrawCentered(device, kGuns[idx], bx, by + 1250.0f, bz,
                                    yaw, 7.0f, true);
        }
    }
    // Wall-buy weapons: real BO1 chalk drawings fixed on the side wall.
    // Not HUD icons: these are the chalk_weapon_* materials from the BO1 fastfiles.
    float anch[3]; WorldMachines_Anchor(anch);
    static const char* kChalk[3] = {
        "hud/chalk/chalk_weapon_mp40_c.dds",
        "hud/chalk/chalk_weapon_m14_c.dds",
        "hud/chalk/chalk_weapon_ithaca_c.dds",
    };
    for (int i = 0; i < 3; ++i) {
        if (!g_chalkTex[i]) {
            char full[MAX_PATH];
            if (BuildAssetPath(full, sizeof(full), kChalk[i]))
                g_chalkTex[i] = LoadDdsTexture(device, full);
        }
        if (!g_chalkTex[i]) continue;
        const float wx = anch[0] - 4200.0f;      // wall at -X
        const float wy = anch[1] + 1100.0f;      // ~1.1 m high
        const float wz = anch[2] + (i - 1) * 1400.0f;
        WorldModel_DrawBillboard(device, g_chalkTex[i], wx, wy, wz, 1.57f, 900.0f, 450.0f);
    }
}

void Overlay_Render(IDirect3DDevice9* device) {
    static bool logged = false;
    if (!logged) { Log("Overlay_Render called"); logged = true; }
    if (!device) return;
    if (!g_fontTexture && !CreateFontAtlas(device)) return;

    // Load the real BO1 crosshair texture (center_cross.dds) lazily.
    if (!g_crosshairTex) {
        char path[MAX_PATH] = {};
        if (BuildAssetPath(path, sizeof(path), "hud/common_zombie/images/center_cross.dds")) {
            g_crosshairTex = LoadDdsTexture(device, path);
        }
    }
    if (!g_scorebarTex) {
        char path[MAX_PATH] = {};
        if (BuildAssetPath(path, sizeof(path), "hud/zombie/scorebar_zom_1.dds")) {
            g_scorebarTex = LoadDdsTexture(device, path);
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (!g_perkTex[i]) {
            char full[MAX_PATH] = {};
            if (BuildAssetPath(full, sizeof(full), g_perkNames[i])) {
                g_perkTex[i] = LoadDdsTexture(device, full);
            }
        }
    }
    // Mystery Box spinning weapon sprites (real BO1 HUD weapon icons).
    static const char* boxIcons[6] = { "hud_colt", "hud_cz75", "hud_spas12", "hud_m14", "hud_uzi", "hud_rpg" };
    for (int i = 0; i < 6; ++i) {
        if (!g_boxWeaponTex[i]) {
            char full[MAX_PATH], rel[160];
            snprintf(rel, sizeof(rel), "hud/code_post_gfx_mp/code_post_gfx_mp/images/%s.dds", boxIcons[i]);
            if (BuildAssetPath(full, sizeof(full), rel))
                g_boxWeaponTex[i] = LoadDdsTexture(device, full);
        }
    }
    if (!g_lowHealthTex) {
        char full[MAX_PATH] = {};
        if (BuildAssetPath(full, sizeof(full), "hud/zombie/overlay_low_health.dds")) {
            g_lowHealthTex = LoadDdsTexture(device, full);
        }
    }
    if (!g_compassHealthTex) {
        char full[MAX_PATH] = {};
        if (BuildAssetPath(full, sizeof(full), "hud/zombie/compasshealthoverlay.dds")) {
            g_compassHealthTex = LoadDdsTexture(device, full);
        }
    }
    Zombies_Update();
    const ZombiesHud& z = Zombies_Get();

    if (z.weaponId != g_ammoCounterId) {
        if (g_ammoCounterTex) { g_ammoCounterTex->Release(); g_ammoCounterTex = nullptr; }
        g_ammoCounterId = z.weaponId;
        char rel[256];
        snprintf(rel, sizeof(rel), "hud/zombie/%s.dds", AmmoCounterName(z.weaponId));
        char full[MAX_PATH] = {};
        if (BuildAssetPath(full, sizeof(full), rel)) {
            g_ammoCounterTex = LoadDdsTexture(device, full);
        }
    }

    static bool prevF7 = false;
    bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (f7 && !prevF7) {
        g_showDebug = !g_showDebug;
    }
    prevF7 = f7;

    // Perk toggles for testing (F1-F4). All four perks are shown by default:
    // the BO1 reference frame displays the four specialty badges.
    static bool prevF[4] = { false, false, false, false };
    static bool perksInit = false;
    if (!perksInit) {
        for (int i = 0; i < 4; ++i) g_perkActive[i] = true;
        perksInit = true;
    }
    const int perkKeys[4] = { VK_F1, VK_F2, VK_F3, VK_F4 };
    for (int i = 0; i < 4; ++i) {
        bool down = (GetAsyncKeyState(perkKeys[i]) & 0x8000) != 0;
        if (down && !prevF[i]) {
            g_perkActive[i] = !g_perkActive[i];
        }
        prevF[i] = down;
    }

    ++g_fpsFrames;
    DWORD now = GetTickCount();
    if (g_lastFpsTick == 0) g_lastFpsTick = now;
    if (now - g_lastFpsTick >= 500) {
        g_fps = g_fpsFrames * 1000.0f / (float)(now - g_lastFpsTick);
        g_fpsFrames = 0;
        g_lastFpsTick = now;
    }

    D3DVIEWPORT9 vp = {};
    if (FAILED(device->GetViewport(&vp)) || vp.Width == 0 || vp.Height == 0) return;
    const float W = (float)vp.Width;
    const float H = (float)vp.Height;

    IDirect3DStateBlock9* savedState = nullptr;
    if (SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL, &savedState)) && savedState) {
        savedState->Capture();
    }

    HRESULT sceneHr = device->BeginScene();
    if (FAILED(sceneHr)) {
        if (!g_beginSceneErrorLogged) {
            Log("overlay: BeginScene failed %08X (overlay skipped)", (unsigned)sceneHr);
            g_beginSceneErrorLogged = true;
        }
        if (savedState) savedState->Release();
        return;
    }

    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);

    std::vector<RectVertex> rects;
    std::vector<TextVertex> text;
    std::vector<TextVertex> glowText;
    rects.reserve(128);
    text.reserve(2048);
    glowText.reserve(256);

    const bool fpsOn = false; // HUD-only: no viewmodel/camera in BO1Z.asi
    void* player = GetPlayer();
    Vec3 p = {};
    bool havePos = ReadPlayerPos(&p);

    // Debug panel (F7).
    if (g_showDebug) {
        AddRect(rects, 18.0f, 18.0f, 660.0f, 300.0f, D3DCOLOR_ARGB(190, 8, 10, 12));
        AddRect(rects, 18.0f, 18.0f, 3.0f, 300.0f, D3DCOLOR_ARGB(255, 180, 30, 25));
        char line[512];
        int n = 0;
        auto addLine = [&](const char* s) {
            AddText(text, 32.0f, 30.0f + n * LINE_H, s, D3DCOLOR_ARGB(255, 235, 235, 235),
                    kBo1FontSmallDef, 0.8f);
            ++n;
        };
        addLine("RE4Z  //  PHASE 1  //  FPS + ZOMBIES HUD");
        snprintf(line, sizeof(line), "base %08X   pPL %08X   pG %08X   player %08X",
                 (unsigned)GameBase(), (unsigned)GetPLGlobal(), (unsigned)GetPGGlobal(),
                 (unsigned)player);
        addLine(line);
        snprintf(line, sizeof(line), "FPS camera: %s   (F6)   FOV 75   eye +1550",
                 fpsOn ? "ON" : "OFF");
        addLine(line);
        if (havePos) {
            snprintf(line, sizeof(line), "Leon  x %.1f  y %.1f  z %.1f", p.x, p.y, p.z);
        } else {
            snprintf(line, sizeof(line), "Leon  (not available)");
        }
        addLine(line);
        if (z.valid) {
            snprintf(line, sizeof(line), "room %04X   round %d   alive %d   kills %d   points %d",
                     z.roomId, z.round, z.alive, z.kills, z.points);
        } else {
            snprintf(line, sizeof(line), "game data (not available)");
        }
        addLine(line);
        if (z.maxHealth > 0) {
            snprintf(line, sizeof(line), "health %d / %d   weapon_no %d   itemId %04X   ammo %d/%d   %.0f fps",
                     z.health, z.maxHealth, z.weaponNo, z.weaponId, z.ammoLoaded,
                     z.ammoReserve, g_fps);
        } else {
            snprintf(line, sizeof(line), "health (n/a)   weapon_no %d   itemId %04X   %.0f fps",
                     z.weaponNo, z.weaponId, g_fps);
        }
        addLine(line);
        addLine("native HUD: F8   force HUD/viewmodel: F9   debug: F7   log: RE4Z.log");
    }

    // HUD-only: no viewmodel rendering (MW2RE4.asi handles the 3D view).

    if (z.inGame && GetPlayer() && IsRangeReadable(GetPlayer(), 0x800)) {
        const float cx = W * 0.5f;
        const float cy = H * 0.5f;
        char buf[64];

        // Low-health red vignette (BO1 does not draw a health bar).
        if (z.healthFrac > 0.0f && z.healthFrac < 0.35f) {
            const float k = (0.35f - z.healthFrac) / 0.35f;
            const int a = (int)(70.0f * k);
            const D3DCOLOR vig = D3DCOLOR_ARGB(a, 130, 0, 0);
            AddRect(rects, 0.0f, 0.0f, W, 70.0f, vig);
            AddRect(rects, 0.0f, H - 70.0f, W, 70.0f, vig);
            AddRect(rects, 0.0f, 0.0f, 70.0f, H, vig);
            AddRect(rects, W - 70.0f, 0.0f, 70.0f, H, vig);
        }

        // ---- BO1 Ascension HUD layout (measured from the reference frame) ----
        // Reference frame is 704x396 (16:9). Layout, in reference pixels:
        //   perk badges : x 53..146, y 289..308  (4 badges, pitch ~25.5)
        //   round number: x 52..100, y 300..353  (big, dark red)
        //   points      : x 1007..1103, y 518..545 (white, in 1920-space)
        //   ammo        : x 918..979, y 668..689 (white, in 1920-space)
        const float refScale = W / 704.0f;   // reference px -> screen px
        const float uiScale = H / 396.0f;
        const float perkX = 53.0f * refScale;
        const float perkY = 277.0f * uiScale;
        const float perkPitch = 25.5f * refScale;
        const float perkSize = 20.0f * refScale;

        // Perk badges (real BO1 specialty_*_zombies art, 32x32 native). A badge
        // shows once its perk is bought (F1-F4 remain a manual test toggle), and
        // pops in with a 0.4 s scale + alpha ease driven by perkPop.
        for (int i = 0; i < 4; ++i) {
            const bool owned = g_perkActive[i] || z.perkOwned[i];
            if (!owned || !g_perkTex[i]) continue;
            float pop = z.perkPop[i];
            if (g_perkActive[i] && !z.perkOwned[i]) pop = 1.0f; // manual toggle: no pop
            if (pop < 0.05f) pop = 0.05f; // bought: the pop starts tiny and grows
            // BO1 specialty pickup: the badge scales up around its centre from
            // ~40% while fading in.
            const float grow = 0.4f + 0.6f * pop;
            const float cx = perkX + i * perkPitch + perkSize * 0.5f;
            const float cy = perkY + perkSize * 0.5f;
            const float half = perkSize * 0.5f * grow;
            const int alpha = (int)(255.0f * (0.25f + 0.75f * pop));
            const D3DCOLOR tint = D3DCOLOR_ARGB(alpha, 255, 255, 255);
            const float x = cx - half, y = cy - half, side = half * 2.0f;
            struct TQV { float x, y, z, rhw; D3DCOLOR c; float u, v; };
            TQV q[4] = {
                { x,      y,           0.0f, 1.0f, tint, 0.0f, 0.0f },
                { x + side, y,         0.0f, 1.0f, tint, 1.0f, 0.0f },
                { x + side, y + side,  0.0f, 1.0f, tint, 1.0f, 1.0f },
                { x,      y + side,    0.0f, 1.0f, tint, 0.0f, 1.0f },
            };
            device->SetTexture(0, g_perkTex[i]);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, q, sizeof(TQV));
            device->SetTexture(0, nullptr);
        }

        // Pack-a-Punch feedback: small label under the round number, not over
        // the perk badges.
        if (z.packaPunched) {
            AddTextCondensed(text, 51.2f * refScale, 365.0f * uiScale, "PACK-A-PUNCHED",
                             D3DCOLOR_ARGB(255, 236, 232, 230), kBo1FontNormalDef,
                             8.0f * uiScale, 5.5f * refScale);
        }

        // Round counter.
        //
        // BO1 keeps the round number in the lower-left corner, UNDER the perk
        // badges (it overlapped them at the previous height): chalk_hud1 shows
        // the chalk shader hud_chalk_N for rounds 1-10
        // and the numeric value for rounds above 10
        // (chalk_one_up, _zombiemode.gsc:3061). The colour is the chalk red
        // (0.21, 0, 0) and it pulses white while the round is over
        // (chalk_round_over, _zombiemode.gsc:3230).
        {
            snprintf(buf, sizeof(buf), "%d", z.round);
            const float pulse = z.roundIntermission ? z.roundPulse : 0.0f;
            const int r = (int)(255.0f * (0.21f + 0.79f * pulse));
            const int g = (int)(255.0f * pulse);
            const D3DCOLOR chalk = D3DCOLOR_ARGB(255, r, g, g);
            const float rs = 42.0f * uiScale;
            const float ra = 23.0f * refScale;
            glowText.clear();
            // Halo passes: BO1's glow material is a soft bloom around the
            // glyph, not a filled shape. The inflation stays small and the
            // alpha low, otherwise the additive blend saturates into a bar.
            // Round number BELOW the perk badges (pushed down so the chalk
            // never overlaps the badges).
            AddTextCondensed(glowText, 51.2f * refScale, 368.0f * uiScale, buf,
                             chalk, kBo1FontNormalDef, rs, ra, 1.5f * uiScale, 0.16f);
            AddTextCondensed(glowText, 51.2f * refScale, 368.0f * uiScale, buf,
                             chalk, kBo1FontNormalDef, rs, ra, 3.0f * uiScale, 0.07f);
            AddTextCondensed(text, 51.2f * refScale, 368.0f * uiScale, buf, chalk,
                             kBo1FontNormalDef, rs, ra);
        }

        // Points: white total, right-aligned; yellow "+N" popup overlapping left.
        {
            // BO1 draws the score on the scorebar_zom_N blood splat: white
            // 512x64 alpha art tinted dark blood red, tapering right where the
            // number sits. Placed behind the score ink (measured 8:1 aspect).
            if (g_scorebarTex) {
                // BO1 score splat: 512x64 alpha art tinted dark blood red.
                // BO1 ref 2560x1440: x 2097..2252 (155px), y 1002..1040 (38px)
                // -> ~43x10.4 ref units. Draw it at that size, not stretched.
                DrawSprite(device, g_scorebarTex, 560.0f * refScale, 268.0f * uiScale,
                           104.0f * refScale, 22.0f * uiScale,
                           D3DCOLOR_ARGB(235, 105, 20, 14));
            }
            snprintf(buf, sizeof(buf), "%d", z.points);
            // Score ink INSIDE the splat, slimmer font (BO1 retail normal font).
            const D3DCOLOR ptsWhite = D3DCOLOR_ARGB(255, 236, 232, 230);
            AddTextCondensedRight(text, 635.0f * refScale, 278.0f * uiScale, buf, ptsWhite,
                                  kBo1FontNormalDef, 9.0f * uiScale, 6.0f * refScale);
            if (z.pointsPopup > 0) {
                // BO1 score popup: right-aligned, yellow (0.9, 0.9, 0), moves
                // up and to the left while it fades out over 0.5 s.
                snprintf(buf, sizeof(buf), "+%d", z.pointsPopup);
                const D3DCOLOR popYellow = D3DCOLOR_ARGB(255, 230, 230, 0);
                // Fade out halfway through the 0.5 s move.
                int alpha = 255;
                if (z.popupAge > 0.25f) {
                    const float t = (z.popupAge - 0.25f) / 0.25f;
                    alpha = (int)(255.0f * (1.0f - (t > 1.0f ? 1.0f : t)));
                }
                const D3DCOLOR col = (alpha >= 255)
                    ? popYellow
                    : (D3DCOLOR)(((DWORD)alpha << 24) | (popYellow & 0x00FFFFFF));
                AddTextCondensedRight(text, (635.0f + z.popupDx) * refScale,
                                     (278.0f + z.popupDy) * uiScale, buf, col,
                                     kBo1FontBoldDef, 11.0f * uiScale, 7.0f * refScale);
            }
        }

        // Bottom-right: weapon and ammo (read from cItemMgr; native RE4 ammo).
        // Load weapon icon lazily (changes with weapon).
        if (z.weaponId != g_weaponIconId) {
            if (g_weaponIconTex) { g_weaponIconTex->Release(); g_weaponIconTex = nullptr; }
            g_weaponIconId = z.weaponId;
            char relPath[256];
            snprintf(relPath, sizeof(relPath), "hud/code_post_gfx_mp/code_post_gfx_mp/images/%s.dds",
                     WeaponIconName(z.weaponId));
            Log("weapon switch: itemId=%04X -> icon=%s", z.weaponId, WeaponIconName(z.weaponId));
            char ipath[MAX_PATH] = {};
            if (BuildAssetPath(ipath, sizeof(ipath), relPath)) {
                g_weaponIconTex = LoadDdsTexture(device, ipath);
            }
        }
        if (g_weaponIconTex) {
            // BO1 does not show a weapon icon in the HUD; keep the icon off-screen.
            const float iw = 0.0f, ih = 0.0f;
            const float ix = -1000.0f, iy = -1000.0f;
            struct TQV { float x, y, z, rhw; D3DCOLOR c; float u, v; };
            TQV q[4] = {
                { ix,      iy,      0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 0.0f, 0.0f },
                { ix + iw, iy,      0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, 0.0f },
                { ix + iw, iy + ih, 0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, 1.0f },
                { ix,      iy + ih, 0.0f, 1.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 0.0f, 1.0f },
            };
            // Drawn after the batch; for now add to rects is not possible (textured).
            // We'll draw it as a separate call below; store the quad for later.
            // For simplicity, draw it now (render state already set for rects, but
            // we need texture). Save/restore via separate draw.
            device->SetTexture(0, g_weaponIconTex);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, q, sizeof(TQV));
            device->SetTexture(0, nullptr);
        } else {
            AddText(text, W - 210.0f, H - 252.0f, Zombies_WeaponName(z.weaponId),
                    D3DCOLOR_ARGB(255, 230, 230, 230), kBo1FontNormalDef, 1.0f);
        }
        if (g_ammoCounterTex) {
            D3DSURFACE_DESC desc = {};
            g_ammoCounterTex->GetLevelDesc(0, &desc);
            // Real BO1 ammo bullet icons sit left of the ammo numbers.
            const float acW = (float)desc.Width * uiScale * 0.9f;
            const float acH = (float)desc.Height * uiScale * 0.9f;
            const float acX = 470.0f * refScale - acW;
            const float acY = 347.0f * uiScale - acH;
            struct TQV { float x, y, z, rhw; D3DCOLOR c; float u, v; };
            TQV q[4] = {
                { acX,      acY,      0.0f, 1.0f, D3DCOLOR_ARGB(230, 255, 255, 255), 0.0f, 0.0f },
                { acX + acW, acY,     0.0f, 1.0f, D3DCOLOR_ARGB(230, 255, 255, 255), 1.0f, 0.0f },
                { acX + acW, acY + acH, 0.0f, 1.0f, D3DCOLOR_ARGB(230, 255, 255, 255), 1.0f, 1.0f },
                { acX,      acY + acH, 0.0f, 1.0f, D3DCOLOR_ARGB(230, 255, 255, 255), 0.0f, 1.0f },
            };
            device->SetTexture(0, g_ammoCounterTex);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, q, sizeof(TQV));
            device->SetTexture(0, nullptr);
        }
        if (z.ammoValid) {
            snprintf(buf, sizeof(buf), "%d/%d", z.ammoLoaded, z.ammoReserve);
        } else {
            snprintf(buf, sizeof(buf), "--/--");
        }
        {
            // BO1 ammo counter: white, right-aligned, under the points.
            // Reference: "117/500" ink x 519..579, baseline y 352, cap 11px.
            // The dev font "bigDevFont" reproduces the reference glyphs: the "1"
            // carries a foot serif and the "7" has a flat top bar. Its native
            // advance is monospaced, so the text is squeezed horizontally.
            AddTextCondensedRight(text, 579.9f * refScale, 352.0f * uiScale, buf,
                                  D3DCOLOR_ARGB(255, 238, 242, 244), kBo1FontDevBigDef,
                                  11.0f * uiScale, 8.8f * refScale);
        }

        // Low-health overlay (full screen red tint) when health < 30%.
        if (z.healthFrac > 0.0f && z.healthFrac < 0.35f && g_lowHealthTex) {
            D3DSURFACE_DESC desc = {};
            g_lowHealthTex->GetLevelDesc(0, &desc);
            struct TQV { float x, y, z, rhw; D3DCOLOR c; float u, v; };
            float alpha = (0.35f - z.healthFrac) / 0.35f * 200.0f;
            if (alpha > 200.0f) alpha = 200.0f;
            D3DCOLOR col = D3DCOLOR_ARGB((int)alpha, 255, 255, 255);
            TQV q[4] = {
                { 0.0f, 0.0f, 0.0f, 1.0f, col, 0.0f, 0.0f },
                { W,    0.0f, 0.0f, 1.0f, col, 1.0f, 0.0f },
                { W,    H,    0.0f, 1.0f, col, 1.0f, 1.0f },
                { 0.0f, H,    0.0f, 1.0f, col, 0.0f, 1.0f },
            };
            device->SetTexture(0, g_lowHealthTex);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, q, sizeof(TQV));
            device->SetTexture(0, nullptr);
        }
        // Compass health overlay (health indicator) when damaged.
        if (z.healthFrac > 0.0f && z.healthFrac < 1.0f && g_compassHealthTex) {
            D3DSURFACE_DESC desc = {};
            g_compassHealthTex->GetLevelDesc(0, &desc);
            const float chW = (float)desc.Width * 0.5f;
            const float chH = (float)desc.Height * 0.5f;
            const float chX = 40.0f, chY = H - 200.0f;
            struct TQV { float x, y, z, rhw; D3DCOLOR c; float u, v; };
            TQV q[4] = {
                { chX,      chY,      0.0f, 1.0f, D3DCOLOR_ARGB(220, 255, 255, 255), 0.0f, 0.0f },
                { chX + chW, chY,     0.0f, 1.0f, D3DCOLOR_ARGB(220, 255, 255, 255), 1.0f, 0.0f },
                { chX + chW, chY + chH, 0.0f, 1.0f, D3DCOLOR_ARGB(220, 255, 255, 255), 1.0f, 1.0f },
                { chX,      chY + chH, 0.0f, 1.0f, D3DCOLOR_ARGB(220, 255, 255, 255), 0.0f, 1.0f },
            };
            device->SetTexture(0, g_compassHealthTex);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, q, sizeof(TQV));
            device->SetTexture(0, nullptr);
        }

        // Crosshair drawn below as a textured quad (real BO1 center_cross.dds).
        // (The old 4-rect crosshair has been replaced.)

        // No crosshair: BO1 zombies has no center crosshair (the player aims
        // with the weapon's own sights). The fake crosshair is removed per the
        // user's request.

        // Hit marker (white on hit, red on kill).
        if (z.hitmarker) {
            const D3DCOLOR hm = z.hitmarkerKill ? D3DCOLOR_ARGB(255, 230, 45, 35)
                                                : D3DCOLOR_ARGB(235, 245, 245, 245);
            AddRect(rects, cx - 20.0f, cy - 14.0f, 8.0f, 2.0f, hm);
            AddRect(rects, cx - 14.0f, cy - 20.0f, 2.0f, 8.0f, hm);
            AddRect(rects, cx + 12.0f, cy - 14.0f, 8.0f, 2.0f, hm);
            AddRect(rects, cx + 12.0f, cy - 20.0f, 2.0f, 8.0f, hm);
            AddRect(rects, cx - 20.0f, cy + 12.0f, 8.0f, 2.0f, hm);
            AddRect(rects, cx - 14.0f, cy + 12.0f, 2.0f, 8.0f, hm);
            AddRect(rects, cx + 12.0f, cy + 12.0f, 8.0f, 2.0f, hm);
            AddRect(rects, cx + 12.0f, cy + 12.0f, 2.0f, 8.0f, hm);
        }

        // Perk-a-Cola buy menu (F8 toggles it in zombies.cpp). BO1's machine
        // panel look: dark panel, red rule, one line per perk with its price.
        if (z.perkMenu) {
            const float mx = W * 0.5f - 160.0f;
            const float my = H * 0.30f;
            const float mw = 320.0f, mh = 150.0f;
            AddRect(rects, mx, my, mw, mh, D3DCOLOR_ARGB(190, 8, 10, 12));
            AddRect(rects, mx, my, mw, 2.0f, D3DCOLOR_ARGB(255, 180, 30, 25));
            AddTextCondensed(text, mx + 16.0f, my + 30.0f * uiScale, "PERK-A-COLA",
                             D3DCOLOR_ARGB(255, 236, 232, 230), kBo1FontBoldDef,
                             12.0f * uiScale, 8.0f * refScale);
            for (int i = 0; i < 4; ++i) {
                const char* name = nullptr;
                int cost = 0;
                bool owned = false;
                Zombies_PerkInfo(i, &name, &cost, &owned);
                char line[80];
                if (owned)
                    snprintf(line, sizeof(line), "%d. %s  -  BUE", i + 1, name);
                else
                    snprintf(line, sizeof(line), "%d. %s  -  %d", i + 1, name, cost);
                // Owned: grey. Affordable: BO1's interact white. Too expensive:
                // dull red, like BO1's disabled buy prompt.
                D3DCOLOR col = D3DCOLOR_ARGB(255, 120, 120, 120);
                if (!owned)
                    col = z.points >= cost ? D3DCOLOR_ARGB(255, 236, 232, 230)
                                           : D3DCOLOR_ARGB(255, 190, 90, 80);
                AddTextCondensed(text, mx + 16.0f, my + (58.0f + 26.0f * i) * uiScale,
                                 line, col, kBo1FontNormalDef, 11.0f * uiScale, 7.5f * refScale);
            }
            AddTextCondensed(text, mx + 16.0f, my + (mh - 12.0f) * uiScale,
                             "F8 : FERMER", D3DCOLOR_ARGB(180, 160, 160, 160),
                             kBo1FontNormalDef, 9.0f * uiScale, 6.0f * refScale);
        }

        // BO1 shows the points popup next to the score, not under the crosshair.

        // D-pad directional indicators removed per user request (the "UP/DN/LT/RT"
        // letters on the right edge were debug-ish leftovers, not BO1 HUD).

        // ---- Points on blood splat (when health < 35%) ----
        if (z.healthFrac > 0.0f && z.healthFrac < 0.35f) {
            snprintf(buf, sizeof(buf), "%d", z.points);
            const D3DCOLOR bloodPts = D3DCOLOR_ARGB(220, 230, 230, 0);
            // Draw points on the lower-left blood splatter area.
            AddTextCondensed(text, 40.0f * refScale, H - 120.0f * uiScale, buf, bloodPts,
                             kBo1FontBoldDef, 16.0f * uiScale, 10.0f * refScale);
        }

    }

    DrawRectBatch(device, rects);
    if (!glowText.empty()) {
        DrawTextGlow(device, glowText);
    }
    {
        // One-shot trace of what the HUD actually submitted, to catch a HUD
        // that is built but never drawn (wrong gate, empty atlas, ...).
        static bool traced = false;
        if (!traced && z.valid && z.inGame) {
            traced = true;
            Log("hud trace: inGame=%d fps=%d textVerts=%zu glowVerts=%zu rects=%zu "
                "retail=%p dev=%p round=%d points=%d ammo=%d/%d",
                z.inGame ? 1 : 0, fpsOn ? 1 : 0, text.size(), glowText.size(),
                rects.size(), (void*)g_fontTexture, (void*)g_devFontTexture,
                z.round, z.points, z.ammoLoaded, z.ammoReserve);
        }
    }
    DrawTextBatch(device, text);

    // BO1-style map intro: a full-screen black veil held for 2.2 s, then a 1.3 s
    // fade (zombies.cpp drives introBlack). Drawn after the text so it covers
    // the whole HUD while the stage settles.
    if (z.introBlack > 0.0f) {
        std::vector<RectVertex> veil;
        AddRect(veil, 0.0f, 0.0f, W, H,
                D3DCOLOR_ARGB((int)(255.0f * z.introBlack), 0, 0, 0));
        DrawRectBatch(device, veil);
    }

    device->SetTexture(0, nullptr);
    device->EndScene();

    // Restore the game's state after EndScene: applying a full state block inside
    // a scene works on most drivers but can confuse the game's own renderer.
    if (savedState) {
        savedState->Apply();
        savedState->Release();
    } else {
        device->SetRenderState(D3DRS_ZENABLE, TRUE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    }
}
