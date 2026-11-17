#include "common.h"
#include "log.h"
#include "game.h"
#include "d3d9hook.h"
#include "bo1_catalog.h"

void MapRedirect_Start();

static HMODULE g_self = nullptr;

static DWORD WINAPI InitThread(LPVOID) {
    LogInit();
    HMODULE exe = GetModuleHandleA(nullptr);
    Log("BO1Z (HUD-only) starting in %s", exe ? "bio4.exe" : "unknown");
    if (!exe) {
        Log("GetModuleHandle(NULL) failed");
        return 0;
    }

    GameInit(exe);
    InstallD3D9Hook(exe);
    MapRedirect_Start();
    Bo1Catalog_Start();
    Log("BO1Z init done (HUD-only + BO1 weapon catalogue + T5 map redirect)");
    return 0;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        // A second copy of this .asi would hook D3D9 twice and publish the catalogue twice:
        // the host would then see the count flip every frame. One copy per process.
        HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\BO1Z_host_singleton");
        if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(single);
            return FALSE; // the loader skips this file
        }
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}