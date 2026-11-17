#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: modlist <pid>\n"); return 1; }
    DWORD pid = (DWORD)strtoul(argv[1], nullptr, 0);
    HANDLE p = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!p) { printf("OpenProcess failed %lu\n", GetLastError()); return 1; }
    HMODULE mods[1024];
    DWORD needed = 0;
    if (!EnumProcessModules(p, mods, sizeof(mods), &needed)) {
        printf("EnumProcessModules failed %lu\n", GetLastError());
        CloseHandle(p);
        return 1;
    }
    int count = (int)(needed / sizeof(HMODULE));
    printf("modules=%d\n", count);
    for (int i = 0; i < count; ++i) {
        char name[MAX_PATH] = {};
        char path[MAX_PATH] = {};
        GetModuleBaseNameA(p, mods[i], name, MAX_PATH);
        GetModuleFileNameExA(p, mods[i], path, MAX_PATH);
        MODULEINFO mi = {};
        GetModuleInformation(p, mods[i], &mi, sizeof(mi));
        printf("%3d  base=%08X  size=%08X  %s\n     %s\n", i,
               (unsigned)(uintptr_t)mi.lpBaseOfDll, (unsigned)mi.SizeOfImage, name, path);
    }
    CloseHandle(p);
    return 0;
}
