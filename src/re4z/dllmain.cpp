#include "common.h"
#include "log.h"
#include "game.h"
#include "camera.h"
#include "d3d9hook.h"
#include "shooting.h"

static HMODULE g_self = nullptr;

static DWORD WINAPI InitThread(LPVOID) {
    LogInit();
    HMODULE exe = GetModuleHandleA(nullptr);
    Log("RE4Z starting in %s", exe ? "bio4.exe" : "unknown");
    if (!exe) {
        Log("GetModuleHandle(NULL) failed");
        return 0;
    }

    GameInit(exe);
    Shooting_Init(exe);
    InstallCameraHook(exe);
    InstallD3D9Hook(exe);
    Log("RE4Z init done");
    return 0;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
