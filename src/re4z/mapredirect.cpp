// Hooks CreateProcessW in RE4's process so that the MW2RE4 host launches the
// runtime with a Black Ops 1 zombie map (T5) instead of an IW4 map. The T5 map
// provides the T5 soldier models the FPV assembly needs, so the BO1 wonder
// weapons (ray gun, etc.) become equippable.

#include "log.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

namespace {

typedef BOOL (WINAPI* CreateProcessWFn)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPCVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
CreateProcessWFn g_orig = nullptr;

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
            auto* byName = (IMAGE_IMPORT_BY_NAME*)((uint8_t*)module + originalThunk->u1.AddressOfData);
            if (strcmp((const char*)byName->Name, wantedFunction) == 0) {
                return (void**)&firstThunk->u1.Function;
            }
        }
    }
    return nullptr;
}

BOOL WINAPI HookCreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES p1, LPSECURITY_ATTRIBUTES p2, BOOL inherit, DWORD flags, LPCVOID env, LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi)
{
    // MW2RE4 sets this only while creating its IW4L child process. Keep the
    // native map for the runtime session: T5 maps run incompatible GSC and can
    // exit before the shared-memory guest becomes ready.
    if (GetEnvironmentVariableW(L"IW4L_RE4_NATIVE_MAP", nullptr, 0) > 0) {
        Log("BO1Z: preserving native IW4 map for IW4L bridge runtime");
        return g_orig(app, cmd, p1, p2, inherit, flags, env, dir, si, pi);
    }
    if (cmd) {
        // Replace the IW4 map name with the light T5 zombie map (Nacht der Untoten).
        const wchar_t* targets[] = { L"mp_subbase", L"mp_rust", L"mp_favela", L"mp_derail", L"mp_invasion" };
        std::wstring patched(cmd);
        for (auto t : targets) {
            auto pos = patched.find(t);
            if (pos != std::wstring::npos) {
                patched.replace(pos, wcslen(t), L"zombie_cod5_prototype");
                Log("BO1Z: map redirected to zombie_cod5_prototype");
                return g_orig(app, const_cast<LPWSTR>(patched.c_str()), p1, p2, inherit, flags, env, dir, si, pi);
            }
        }
    }
    return g_orig(app, cmd, p1, p2, inherit, flags, env, dir, si, pi);
}

} // namespace

void MapRedirect_Start()
{
    HMODULE self = GetModuleHandleA(nullptr);
    void** slot = FindIAT(self, "kernel32.dll", "CreateProcessW");
    if (!slot) {
        Log("BO1Z: CreateProcessW IAT entry not found");
        return;
    }
    g_orig = (CreateProcessWFn)*slot;
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        Log("BO1Z: VirtualProtect(IAT) failed %lu", GetLastError());
        return;
    }
    *slot = (void*)&HookCreateProcessW;
    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
    Log("BO1Z: CreateProcessW IAT hooked");
}
