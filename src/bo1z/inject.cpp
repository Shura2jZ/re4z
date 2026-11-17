// Tiny 32-bit injector for the BO1Z probe.
//
//   bo1z_inject.exe <pid> <dll>
//   bo1z_inject.exe --watch <dll> [process-name]
//
// The watch mode is meant to be started before Plutonium launches t5sp.exe.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>

static DWORD FindProcess(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool Inject(DWORD pid, const char* dll) {
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                              PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                              FALSE, pid);
    if (!proc) {
        printf("OpenProcess(%lu) failed: %lu\n", pid, GetLastError());
        return false;
    }
    size_t len = strlen(dll) + 1;
    void* remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (!remote) {
        printf("VirtualAllocEx failed: %lu\n", GetLastError());
        CloseHandle(proc);
        return false;
    }
    if (!WriteProcessMemory(proc, remote, dll, len, nullptr)) {
        printf("WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    FARPROC loadLibrary = GetProcAddress(kernel32, "LoadLibraryA");
    HANDLE th = CreateRemoteThread(proc, nullptr, 0,
                                   (LPTHREAD_START_ROUTINE)loadLibrary, remote, 0, nullptr);
    if (!th) {
        printf("CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }
    WaitForSingleObject(th, 10000);
    DWORD exitCode = 0;
    GetExitCodeThread(th, &exitCode);
    CloseHandle(th);
    // Do not free the remote string too early in some loaders; leaking it is fine.
    printf("injected into pid %lu (LoadLibrary returned 0x%08lX)\n", pid, exitCode);
    CloseHandle(proc);
    return true;
}

int main(int argc, char** argv) {
    if (argc == 4 && strcmp(argv[1], "--watch") == 0) {
        const char* dll = argv[2];
        const char* name = argv[3];
        printf("watching for %s, dll=%s\n", name, dll);
        for (;;) {
            DWORD pid = FindProcess(name);
            if (pid) {
                printf("found %s pid=%lu\n", name, pid);
                // Give the loader a moment to finish mapping the image.
                Sleep(150);
                if (Inject(pid, dll)) return 0;
            }
            Sleep(100);
        }
    }
    if (argc == 3) {
        DWORD pid = (DWORD)strtoul(argv[1], nullptr, 0);
        return Inject(pid, argv[2]) ? 0 : 1;
    }
    printf("usage: %s <pid> <dll>\n", argv[0]);
    printf("       %s --watch <dll> <process-name>\n", argv[0]);
    return 1;
}
