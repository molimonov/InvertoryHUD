#include <Windows.h>
#include <TlHelp32.h>

#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::wstring DirectoryOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring ModulePath()
{
    wchar_t buffer[MAX_PATH * 4] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return std::wstring(buffer, length);
}

DWORD FindProcess(const wchar_t* name)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, name) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

bool Inject(DWORD pid, const std::wstring& dll_path)
{
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                     PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (process == nullptr) {
        wprintf(L"OpenProcess failed: %lu\n", GetLastError());
        return false;
    }

    const size_t bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == nullptr) {
        wprintf(L"VirtualAllocEx failed: %lu\n", GetLastError());
        CloseHandle(process);
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, dll_path.c_str(), bytes, &written) || written != bytes) {
        wprintf(L"WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(kernel, "LoadLibraryW"));
    if (load_library == nullptr) {
        wprintf(L"LoadLibraryW not found\n");
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr);
    if (thread == nullptr) {
        wprintf(L"CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    wprintf(L"Waiting for LoadLibraryW...\n");
    WaitForSingleObject(thread, 15000);
    DWORD exit_code = 0;
    GetExitCodeThread(thread, &exit_code);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);

    if (exit_code == 0) {
        wprintf(L"DLL load failed (LoadLibraryW returned 0)\n");
        return false;
    }
    wprintf(L"DLL loaded, HMODULE=0x%08lX\n", exit_code);
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    bool launch = false;
    std::wstring process_name = L"s2dng_addon.exe";
    std::wstring dll_path;

    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--launch") == 0) {
            launch = true;
        } else if (process_name == L"s2dng_addon.exe") {
            process_name = argv[i];
        } else if (dll_path.empty()) {
            dll_path = argv[i];
        }
    }

    const std::wstring injector_dir = DirectoryOf(ModulePath());
    if (dll_path.empty()) {
        dll_path = injector_dir + L"\\InvertoryHUD.dll";
    }

    if (launch) {
        const std::wstring exe = injector_dir + L"\\" + process_name;
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info = {};
        std::wstring command = L"\"" + exe + L"\"";
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, injector_dir.c_str(),
                            &startup, &info)) {
            wprintf(L"CreateProcessW failed: %lu\n", GetLastError());
            return 1;
        }
        CloseHandle(info.hThread);
        wprintf(L"Started %s (PID %lu)\n", exe.c_str(), info.dwProcessId);
        CloseHandle(info.hProcess);
        Sleep(1500);
    }

    DWORD pid = FindProcess(process_name.c_str());
    if (pid == 0 && process_name == L"s2dng_addon.exe") {
        pid = FindProcess(L"s2dng.exe");
        if (pid != 0) {
            process_name = L"s2dng.exe";
        }
    }
    if (pid == 0) {
        wprintf(L"Process %s is not running.\n", process_name.c_str());
        wprintf(L"Usage: Injector.exe [--launch] [process.exe] [path\\to\\InvertoryHUD.dll]\n");
        return 1;
    }

    wprintf(L"Target: %s (PID %lu)\n", process_name.c_str(), pid);
    wprintf(L"DLL:    %s\n", dll_path.c_str());
    return Inject(pid, dll_path) ? 0 : 1;
}
