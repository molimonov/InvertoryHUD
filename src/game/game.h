#pragma once

#include <cstdint>
#include <string>

namespace game {

// Addresses are RVAs from the preferred image base 0x00400000 of
// s2dng_addon.exe (32-bit, no ASLR, no relocation table). They are verified
// against the binary documented in docs/INVENTORY_RESEARCH.md.
// Ghidra lists absolute virtual addresses assuming the preferred image base
// 0x00400000. The constants below are RVAs so that they still work if the
// loader ever relocates the image.
constexpr uintptr_t kImageBase = 0x00400000;

namespace addr {
constexpr uintptr_t kGoodAmountLookup = 0x00461FB0 - kImageBase; // 0x00061FB0
constexpr uintptr_t kGameManager = 0x006CAC60 - kImageBase;      // 0x002CAC60
constexpr uintptr_t kGraphicDevice = 0x006D0854 - kImageBase;    // 0x002D0854
} // namespace addr

namespace offsets {
constexpr uintptr_t kPlayerArray = 0x14;   // CLogicGame::m_players
constexpr uintptr_t kPlayerStock = 0x114;  // per-player std::map<GoodId, Amount>
constexpr uintptr_t kPlayerActive = 0x164; // byte, relative to CPlayer*
constexpr uintptr_t kPlayerType = 0xC;     // int, 2 = local human
constexpr uintptr_t kPlayerIndex = 0x4;    // int, also the offset of player[0]
                                           // inside the players array
constexpr uintptr_t kGraphicDevice = 0xB0; // IDirect3DDevice9*
constexpr uint32_t kPlayerStride = 500;    // 0x1F4
constexpr uint32_t kMaxPlayers = 8;
} // namespace offsets

constexpr uint32_t kExpectedSizeOfImage = 0x0032E000;
constexpr uint32_t kExpectedTimeDateStamp = 0x45BF9600;

struct Context {
    bool supported = false;
    uintptr_t base = 0;
    uintptr_t game_manager = 0;
    uintptr_t players = 0;
    uintptr_t player = 0;
    int player_index = -1;
};

bool Initialize();
bool IsSupported();
uintptr_t Base();
const char* CompatibilityError();
Context ReadContext();

} // namespace game
