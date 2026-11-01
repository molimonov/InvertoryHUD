#include "common/logger.h"
#include "common/util.h"
#include "game/game.h"
#include "render/d3d9_hook.h"
#include "render/hud.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <iterator>

namespace {

HMODULE g_module = nullptr;
volatile LONG g_stop = 0;
HANDLE g_worker = nullptr;
PVOID g_veh = nullptr;
char g_crash_log_path[MAX_PATH * 4] = {};

void WriteCrashLog(const char* text)
{
    if (g_crash_log_path[0] == '\0') {
        return;
    }
    HANDLE file = CreateFileA(g_crash_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
    CloseHandle(file);
}

LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* info)
{
    if (info == nullptr || info->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == 0xE06D7363u) { // C++ exception: not interesting here
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const uintptr_t address = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    char module_name[MAX_PATH] = {};
    HMODULE fault_module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &fault_module) &&
        fault_module != nullptr) {
        GetModuleFileNameA(fault_module, module_name, static_cast<DWORD>(std::size(module_name)));
    }

    char line[1024] = {};
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "VEH: code=0x%08X address=0x%08X module=%s thread=%lu\n", static_cast<unsigned>(code),
                static_cast<unsigned>(address), module_name[0] != '\0' ? module_name : "?",
                GetCurrentThreadId());
    WriteCrashLog(line);
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI Worker(LPVOID)
{
    const std::wstring directory = util::GetModuleDirectory(g_module);
    logger::Init(util::JoinPath(directory, L"InvertoryHUD.log"));
    const std::string crash_path = util::WideToAnsi(util::JoinPath(directory, L"InvertoryHUD_crash.log"));
    _snprintf_s(g_crash_log_path, sizeof(g_crash_log_path), _TRUNCATE, "%s", crash_path.c_str());
    DeleteFileA(g_crash_log_path);
    g_veh = AddVectoredExceptionHandler(1, CrashHandler);
    logger::Info("InvertoryHUD 0.1.0 worker starting; module=%s", util::WideToUtf8(directory).c_str());

    if (!game::Initialize()) {
        logger::Error("Compatibility check failed: %s", game::CompatibilityError());
        return 0;
    }

    hud::SetModule(g_module);

    uint64_t last_install_attempt = 0;
    while (InterlockedCompareExchange(&g_stop, 0, 0) == 0) {
        const uint64_t now = util::NowMs();
        if (!d3d9_hook::IsInstalled() || now - last_install_attempt > 2000) {
            last_install_attempt = now;
            d3d9_hook::Install();
        }
        Sleep(250);
    }

    logger::Info("InvertoryHUD worker stopping");
    d3d9_hook::Uninstall();
    hud::Shutdown();
    if (g_veh != nullptr) {
        RemoveVectoredExceptionHandler(g_veh);
        g_veh = nullptr;
    }
    logger::Shutdown();
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (g_worker != nullptr) {
            CloseHandle(g_worker);
            g_worker = nullptr;
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_stop, 1);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void InvertoryHUD_ReloadConfig()
{
    hud::ReloadConfig();
}

extern "C" __declspec(dllexport) void InvertoryHUD_Shutdown()
{
    InterlockedExchange(&g_stop, 1);
    d3d9_hook::Uninstall();
    hud::Shutdown();
}

extern "C" __declspec(dllexport) const char* InvertoryHUD_Version()
{
    return "0.1.0";
}
