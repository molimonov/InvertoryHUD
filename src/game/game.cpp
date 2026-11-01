#include "game/game.h"

#include "common/logger.h"
#include "common/util.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace game {
namespace {

bool g_initialized = false;
bool g_supported = false;
uintptr_t g_base = 0;
char g_error[256] = {};

bool CheckModuleName(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    return _wcsicmp(name.c_str(), L"s2dng_addon.exe") == 0;
}

} // namespace

bool Initialize()
{
    if (g_initialized) {
        return g_supported;
    }
    g_initialized = true;

    HMODULE exe = GetModuleHandleW(nullptr);
    g_base = reinterpret_cast<uintptr_t>(exe);
    const std::wstring path = util::GetModulePath(nullptr);
    if (!CheckModuleName(path)) {
        _snprintf_s(g_error, sizeof(g_error), _TRUNCATE,
                    "Unsupported executable: %s (expected s2dng_addon.exe)",
                    util::WideToUtf8(path).c_str());
        logger::Error("%s", g_error);
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        _snprintf_s(g_error, sizeof(g_error), _TRUNCATE, "Invalid DOS header");
        logger::Error("%s", g_error);
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        _snprintf_s(g_error, sizeof(g_error), _TRUNCATE, "Unsupported PE header");
        logger::Error("%s", g_error);
        return false;
    }
    if (nt->OptionalHeader.SizeOfImage != kExpectedSizeOfImage ||
        nt->FileHeader.TimeDateStamp != kExpectedTimeDateStamp) {
        _snprintf_s(g_error, sizeof(g_error), _TRUNCATE,
                    "Version mismatch: SizeOfImage=0x%08X TimeDateStamp=0x%08X (expected 0x%08X/0x%08X)",
                    nt->OptionalHeader.SizeOfImage, nt->FileHeader.TimeDateStamp, kExpectedSizeOfImage,
                    kExpectedTimeDateStamp);
        logger::Error("%s", g_error);
        return false;
    }

    g_supported = true;
    logger::Info("Game module: %s", util::WideToUtf8(path).c_str());
    logger::Info("Game base: 0x%08X, image size 0x%08X, timestamp 0x%08X", static_cast<unsigned>(g_base),
                 nt->OptionalHeader.SizeOfImage, nt->FileHeader.TimeDateStamp);
    return true;
}

bool IsSupported()
{
    return g_supported;
}

uintptr_t Base()
{
    return g_base;
}

const char* CompatibilityError()
{
    return g_error;
}

Context ReadContext()
{
    Context context;
    context.supported = g_supported;
    context.base = g_base;
    if (!g_supported) {
        return context;
    }

    uintptr_t manager = 0;
    if (!util::SafeRead(g_base + addr::kGameManager, manager) || manager == 0) {
        return context;
    }
    context.game_manager = manager;

    uintptr_t players = 0;
    if (!util::SafeRead(manager + offsets::kPlayerArray, players) || players == 0) {
        return context;
    }
    context.players = players;

    int fallback = -1;
    for (uint32_t i = 0; i < offsets::kMaxPlayers; ++i) {
        const uintptr_t player = players + offsets::kPlayerIndex + i * offsets::kPlayerStride;
        uint8_t active = 0;
        int type = -1;
        if (!util::SafeRead(player + offsets::kPlayerActive, active) ||
            !util::SafeRead(player + offsets::kPlayerType, type)) {
            continue;
        }
        if (active == 0) {
            continue;
        }
        if (fallback < 0) {
            fallback = static_cast<int>(i);
        }
        if (type == 2) {
            context.player = player;
            context.player_index = static_cast<int>(i);
            return context;
        }
    }

    if (fallback >= 0) {
        context.player = players + offsets::kPlayerIndex + static_cast<uint32_t>(fallback) * offsets::kPlayerStride;
        context.player_index = fallback;
    }
    return context;
}

} // namespace game
