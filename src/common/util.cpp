#include "common/util.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <iterator>

namespace util {

std::wstring GetModulePath(HMODULE module)
{
    wchar_t buffer[MAX_PATH * 4] = {};
    DWORD length = GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0) {
        return {};
    }
    return std::wstring(buffer, length);
}

std::wstring GetModuleDirectory(HMODULE module)
{
    const std::wstring path = GetModulePath(module);
    if (path.empty()) {
        return {};
    }
    return std::filesystem::path(path).parent_path().wstring();
}

std::wstring GetGameRoot()
{
    // The DLL lives in <game>\bin. If that layout is not present, fall back to
    // the directory of the process executable.
    const std::wstring module_dir = GetModuleDirectory(nullptr);
    if (module_dir.empty()) {
        return {};
    }
    std::filesystem::path dir(module_dir);
    if (_wcsicmp(dir.filename().c_str(), L"bin") == 0) {
        return dir.parent_path().wstring();
    }
    return dir.wstring();
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b)
{
    return (std::filesystem::path(a) / b).wstring();
}

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out, size_t max_size)
{
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<uint64_t>(size.QuadPart) > max_size) {
        CloseHandle(file);
        return false;
    }

    out.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < out.size()) {
        DWORD read = 0;
        const DWORD request = static_cast<DWORD>(std::min<size_t>(out.size() - offset, 1u << 20));
        if (!ReadFile(file, out.data() + offset, request, &read, nullptr) || read == 0) {
            CloseHandle(file);
            out.clear();
            return false;
        }
        offset += read;
    }

    CloseHandle(file);
    return true;
}

std::string WideToAnsi(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_ACP, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_ACP, 0, value.data(), static_cast<int>(value.size()), result.data(), length,
                        nullptr, nullptr);
    return result;
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length,
                        nullptr, nullptr);
    return result;
}

std::wstring AnsiToWide(const std::string& value)
{
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

bool IsReadable(uintptr_t address, size_t size)
{
    if (address == 0 || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info = {};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
        return false;
    }
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        return false;
    }
    const uintptr_t start = reinterpret_cast<uintptr_t>(info.BaseAddress);
    const uintptr_t end = start + info.RegionSize;
    return address >= start && address + size <= end;
}

uint64_t NowMs()
{
    return GetTickCount64();
}

} // namespace util
