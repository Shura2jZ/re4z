// Publishes a curated weapon catalogue into the MW2RE4 shared memory.
//
// The runtime publishes the first 128 rows of its weapon registry, which is the
// MW2 class list (one entry per weapon + attachment). That list never contains
// the Black Ops 1 zombie weapons, so the wonder weapons (ray gun) and the
// Pack-a-Punch variants are unreachable from the in-game weapon menu.
//
// The registry rows themselves are known (see bo1_catalog.h), so the ASI can
// publish its own list: the BO1 zombie weapons first, then the MW2 roster.
// The host re-reads the catalogue whenever the count changes, so the count is
// set to a value the runtime never uses.

#include "bo1_catalog.h"
#include "log.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kFrameOffset = 8192;
constexpr uint32_t kMaxWidth = 1280, kMaxHeight = 720;
constexpr uint32_t kSceneOffset = 2097152;
constexpr uint32_t kTriangleOffset = kSceneOffset + 64;
constexpr uint32_t kMaxTriangles = 8192;
constexpr uint32_t kFeaturesOffset = kTriangleOffset + kMaxTriangles * 40;
constexpr uint32_t kCatalogOffset = kFeaturesOffset + 64;
constexpr uint32_t kCatalogSlots = 128;

struct CatalogEntry {
    uint32_t weapon;
    uint32_t kind;
    char name[56];
};

struct Features {
    uint32_t request, kind, weapon, ack, outcome, catalogCount, projectileCount, explosions, tick;
    int32_t lethalAmmo, tacticalAmmo;
    uint32_t reserved[5];
};

} // namespace

static HANDLE g_mapping = nullptr;
static HANDLE g_mutex = nullptr;
static uint8_t* g_view = nullptr;
static bool g_logged = false;

static void OpenShared()
{
    if (g_view) return;
    g_mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"Local\\MW2RE4_v1");
    if (!g_mapping) return;
    g_mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\MW2RE4_v1_control");
    g_view = (uint8_t*)MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!g_view) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return;
    }
    Log("BO1Z: shared catalogue mapped");
}

static void PublishLocked()
{
    auto* features = (Features*)(g_view + kFeaturesOffset);
    auto* catalog = (CatalogEntry*)(g_view + kCatalogOffset);
    const uint32_t count = kBo1CatalogCount < kCatalogSlots ? kBo1CatalogCount : kCatalogSlots;
    for (uint32_t i = 0; i < count; ++i) {
        catalog[i].weapon = kBo1Catalog[i].row;
        catalog[i].kind = kBo1Catalog[i].kind;
        memset(catalog[i].name, 0, sizeof(catalog[i].name));
        strncpy(catalog[i].name, kBo1Catalog[i].name, sizeof(catalog[i].name) - 1);
    }
    // The runtime publishes 128; a different count makes the host re-read.
    features->catalogCount = count;
    if (!g_logged) {
        g_logged = true;
        Log("BO1Z: catalogue published (%u entries, first = %s)", count, kBo1Catalog[0].name);
    }
}

void Bo1Catalog_Publish()
{
    OpenShared();
    if (!g_view) return;
    if (g_mutex) WaitForSingleObject(g_mutex, 20);
    PublishLocked();
    if (g_mutex) ReleaseMutex(g_mutex);
}

// Keeps the catalogue ours even if the runtime republishes after the frame:
// a background thread writes it right before the host reads it.
static DWORD WINAPI Bo1Catalog_Thread(LPVOID)
{
    while (true) {
        Bo1Catalog_Publish();
        Sleep(3);
    }
    return 0;
}

void Bo1Catalog_Start()
{
    CreateThread(nullptr, 0, Bo1Catalog_Thread, nullptr, 0, nullptr);
}
