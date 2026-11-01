#include "common/logger.h"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <share.h>

namespace logger {
namespace {

FILE* g_file = nullptr;
CRITICAL_SECTION g_lock = {};
bool g_lock_ready = false;

const char* LevelName(const char* level)
{
    return level;
}

void Write(const char* level, const char* format, va_list args)
{
    char body[4096] = {};
    vsnprintf(body, sizeof(body), format, args);

    SYSTEMTIME time = {};
    GetLocalTime(&time);
    char line[4352] = {};
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02u:%02u:%02u.%03u] [%s] %s\n", time.wHour, time.wMinute,
                time.wSecond, time.wMilliseconds, LevelName(level), body);

    if (g_lock_ready) {
        EnterCriticalSection(&g_lock);
    }
    OutputDebugStringA(line);
    if (g_file != nullptr) {
        fputs(line, g_file);
        fflush(g_file);
    }
    if (g_lock_ready) {
        LeaveCriticalSection(&g_lock);
    }
}

} // namespace

bool Init(const std::wstring& path)
{
    if (!g_lock_ready) {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = true;
    }

    if (g_file != nullptr) {
        fclose(g_file);
        g_file = nullptr;
    }

    g_file = _wfsopen(path.c_str(), L"w", _SH_DENYNO);
    if (g_file == nullptr) {
        return false;
    }

    Info("InvertoryHUD logger started");
    return true;
}

void Shutdown()
{
    if (g_file != nullptr) {
        fclose(g_file);
        g_file = nullptr;
    }
}

void Info(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Write("INFO", format, args);
    va_end(args);
}

void Warn(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Write("WARN", format, args);
    va_end(args);
}

void Error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Write("ERROR", format, args);
    va_end(args);
}

} // namespace logger
