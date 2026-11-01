#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace util {

std::wstring GetModulePath(HMODULE module);
std::wstring GetModuleDirectory(HMODULE module);
std::wstring GetGameRoot();
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
bool FileExists(const std::wstring& path);
bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out, size_t max_size = 64u * 1024u * 1024u);
std::string WideToAnsi(const std::wstring& value);
std::string WideToUtf8(const std::wstring& value);
std::wstring AnsiToWide(const std::string& value);

// Reads a POD value from an arbitrary address. Used for game structures whose
// pointers come from the running process. Never throws; faults are contained
// by SEH.
template <typename T>
bool SafeRead(uintptr_t address, T& out)
{
    __try {
        out = *reinterpret_cast<const T*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsReadable(uintptr_t address, size_t size = sizeof(void*));

uint64_t NowMs();

} // namespace util
