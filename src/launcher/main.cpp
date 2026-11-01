#include <Windows.h>
#include <shellapi.h>

#include <iterator>
#include <string>

namespace {

std::wstring ModuleDirectory()
{
    wchar_t buffer[MAX_PATH * 4] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    std::wstring path(buffer, length);
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool InjectDll(HANDLE process, const std::wstring& dll_path)
{
    const size_t bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == nullptr) {
        return false;
    }
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, dll_path.c_str(), bytes, &written) || written != bytes) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const auto load_library =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel, "LoadLibraryW"));
    if (load_library == nullptr) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr);
    if (thread == nullptr) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }
    WaitForSingleObject(thread, 15000);
    DWORD exit_code = 0;
    GetExitCodeThread(thread, &exit_code);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return exit_code != 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    const std::wstring directory = ModuleDirectory();
    const std::wstring game_exe = directory + L"\\s2dng_addon.exe";
    const std::wstring dll_path = directory + L"\\InvertoryHUD.dll";
    if (!FileExists(game_exe)) {
        MessageBoxW(nullptr, L"s2dng_addon.exe not found next to S2_HUD.exe.", L"InvertoryHUD",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!FileExists(dll_path)) {
        MessageBoxW(nullptr, L"InvertoryHUD.dll not found next to S2_HUD.exe.", L"InvertoryHUD",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring command_line = L"\"" + game_exe + L"\"";
    if (argv != nullptr) {
        for (int i = 1; i < argc; ++i) {
            command_line += L" ";
            command_line += argv[i];
        }
        LocalFree(argv);
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process_info = {};
    if (!CreateProcessW(game_exe.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        directory.c_str(), &startup, &process_info)) {
        MessageBoxW(nullptr, L"Could not start s2dng_addon.exe.", L"InvertoryHUD", MB_OK | MB_ICONERROR);
        return 1;
    }

    CloseHandle(process_info.hThread);
    Sleep(1500);
    const bool injected = InjectDll(process_info.hProcess, dll_path);
    CloseHandle(process_info.hProcess);
    if (!injected) {
        MessageBoxW(nullptr,
                    L"The game was started, but InvertoryHUD.dll could not be injected.\n"
                    L"Start the game as administrator or run Injector.exe manually.",
                    L"InvertoryHUD", MB_OK | MB_ICONWARNING);
    }
    return injected ? 0 : 1;
}
