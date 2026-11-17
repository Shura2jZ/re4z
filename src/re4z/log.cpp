#include "log.h"
#include "common.h"

#include <cstdio>
#include <cstring>
#include <string>

static FILE* g_log = nullptr;
static CRITICAL_SECTION g_logCs;
static bool g_logCsReady = false;

static std::string GetLogPath() {
    char path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return "RE4Z.log";
    }
    char* slash = strrchr(path, '\\');
    if (slash) {
        slash[1] = 0;
    }
    return std::string(path) + "RE4Z.log";
}

void LogInit() {
    if (!g_logCsReady) {
        InitializeCriticalSection(&g_logCs);
        g_logCsReady = true;
    }
    EnterCriticalSection(&g_logCs);
    if (!g_log) {
        std::string path = GetLogPath();
        g_log = fopen(path.c_str(), "w");
    }
    if (g_log) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(g_log, "=== RE4Z %04d-%02d-%02d %02d:%02d:%02d ===\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fflush(g_log);
    }
    LeaveCriticalSection(&g_logCs);
}

void LogShutdown() {
    if (!g_logCsReady) return;
    EnterCriticalSection(&g_logCs);
    if (g_log) {
        fclose(g_log);
        g_log = nullptr;
    }
    LeaveCriticalSection(&g_logCs);
}

void Log(const char* fmt, ...) {
    if (!g_logCsReady) return;
    EnterCriticalSection(&g_logCs);
    if (g_log) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fputc('\n', g_log);
        fflush(g_log);
    }
    LeaveCriticalSection(&g_logCs);
}
