#pragma once

#include <cstdarg>

void LogInit();
void LogShutdown();
void Log(const char* fmt, ...);
