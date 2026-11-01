#include "game/inventory.h"

#include "common/logger.h"
#include "common/util.h"
#include "game/game.h"

#include <Windows.h>

#include <climits>
#include <cstdio>
#include <iterator>

namespace inventory {
namespace {

// Goods are created by data/scripts/properties/Goods.lua via
// properties.gp_create("..."). The 32-bit value is the map key used by
// CPlayer::goods (player + 0x114) and by logic.pl_getGoodAmount().
const ResourceDef kDefinitions[] = {
    {"wood", 0xCD6EC133, 0, 0, "Wood"},
    {"plank", 0xC5C945A3, 1, 0, "Plank"},
    {"stone", 0x55E952D3, 2, 0, "Stone"},
    {"pig", 0xBB5D0683, 3, 0, "Pig"},
    {"corn", 0x17626A03, 4, 0, "Corn"},
    {"flour", 0xE70C72D3, 5, 0, "Flour"},
    {"fish", 0x4012E5A3, 6, 0, "Fish"},
    {"flesh", 0xAD0D6C63, 7, 0, "Flesh"},
    {"bread", 0x90AB8923, 8, 0, "Bread"},
    {"water", 0xA9676263, 9, 0, "Water"},
    {"beer", 0x58647DA3, 0, 1, "Beer"},
    {"ironOre", 0xEC5020BE, 1, 1, "Iron ore"},
    {"gold", 0x4F41C633, 2, 1, "Gold"},
    {"coal", 0x7068DCD3, 3, 1, "Coal"},
    {"iron", 0x8198BF23, 4, 1, "Iron"},
    {"coin", 0x499D4C63, 5, 1, "Coin"},
    {"gripper", 0x6CC2D2D3, 6, 1, "Gripper"},
    {"axe", 0xE1281613, 7, 1, "Axe"},
    {"saw", 0x3788FFC3, 8, 1, "Saw"},
    {"pickaxe", 0x45D899E3, 9, 1, "Pickaxe"},
    {"hammer", 0x69C15033, 0, 2, "Hammer"},
    {"shovel", 0x67C04E73, 1, 2, "Shovel"},
    {"pot", 0xED76AE73, 2, 2, "Pot"},
    {"fishingRod", 0xF3F3DCC3, 3, 2, "Fishing rod"},
    {"scythe", 0x1CEACD43, 4, 2, "Scythe"},
    {"hatchet", 0xF2B7453E, 5, 2, "Hatchet"},
    {"rollingPin", 0xC2049323, 6, 2, "Rolling pin"},
    {"spear", 0x79F34393, 7, 2, "Spear"},
    {"sword", 0xA8483903, 8, 2, "Sword"},
    {"shield", 0x1EEBCF03, 9, 2, "Shield"},
    {"boat", 0xF63DE553, 0, 3, "Boat"},
    {"ship", 0x1D66B4AE, 1, 3, "Ship"},
    {"donkey", 0x2448356E, 5, 3, "Donkey"},
};

// The game's logic.pl_getGoodAmount() reaches CPlayer::goods through
// FUN_00461FB0, which performs a std::map lookup. Calling that function from
// the render thread proved unreliable (SEH fault inside the map), so this is a
// read-only traversal of the same tree. The layout below is taken directly
// from the disassembly of FUN_00585130 / FUN_00461FB0:
//   player + 0x114 : map owner
//   owner + 0x008  : std::map object
//   owner + 0x00C  : map header (end node)
//   header + 0x004 : root
//   node + 0x000   : left
//   node + 0x008   : right
//   node + 0x00C   : int key (goods id)
//   node + 0x010   : int value (amount)
//   node + 0x015   : is_nil byte
int ReadStockAmount(uintptr_t player, int good_id)
{
    const uintptr_t stock = player + game::offsets::kPlayerStock;
    uintptr_t header = 0;
    if (!util::SafeRead(stock + 0xC, header) || header == 0) {
        return INT_MIN;
    }
    uintptr_t node = 0;
    if (!util::SafeRead(header + 4, node)) {
        return INT_MIN;
    }

    uintptr_t lower = header;
    for (int depth = 0; depth < 64; ++depth) {
        if (node == header || node == 0) {
            break;
        }
        uint8_t nil = 0;
        if (!util::SafeRead(node + 0x15, nil) || nil != 0) {
            break;
        }
        int node_key = 0;
        if (!util::SafeRead(node + 0xC, node_key)) {
            return INT_MIN;
        }
        if (node_key < good_id) {
            uintptr_t right = 0;
            if (!util::SafeRead(node + 0x8, right)) {
                return INT_MIN;
            }
            node = right;
        } else {
            lower = node;
            uintptr_t left = 0;
            if (!util::SafeRead(node, left)) {
                return INT_MIN;
            }
            node = left;
        }
    }

    if (lower == header) {
        return 0;
    }
    int lower_key = 0;
    if (!util::SafeRead(lower + 0xC, lower_key)) {
        return INT_MIN;
    }
    if (lower_key != good_id) {
        return 0;
    }
    int value = 0;
    if (!util::SafeRead(lower + 0x10, value)) {
        return INT_MIN;
    }
    return value;
}

} // namespace

const ResourceValue* Snapshot::Find(const std::string& key) const
{
    for (const ResourceValue& value : values) {
        if (value.def != nullptr && key == value.def->key) {
            return &value;
        }
    }
    return nullptr;
}

const ResourceDef* Definitions()
{
    return kDefinitions;
}

size_t DefinitionCount()
{
    return std::size(kDefinitions);
}

const ResourceDef* FindDefinition(const std::string& key)
{
    for (const ResourceDef& def : kDefinitions) {
        if (key == def.key) {
            return &def;
        }
    }
    return nullptr;
}

void Reader::SetCompatible(bool compatible)
{
    compatible_ = compatible;
    if (!compatible) {
        snapshot_.valid = false;
        previous_player_ = -1;
        previous_amounts_.clear();
    }
}

void Reader::Update(bool force)
{
    const uint64_t now = util::NowMs();
    if (!force && now - last_update_ < 500) {
        return;
    }
    last_update_ = now;

    if (!compatible_ || !game::IsSupported()) {
        snapshot_.valid = false;
        return;
    }

    const game::Context context = game::ReadContext();
    static uint64_t last_diag = 0;
    if (now - last_diag > 2000) {
        last_diag = now;
        logger::Info("Diag: manager=0x%08X players=0x%08X player=0x%08X index=%d",
                     static_cast<unsigned>(context.game_manager), static_cast<unsigned>(context.players),
                     static_cast<unsigned>(context.player), context.player_index);
        if (context.players != 0) {
            for (uint32_t i = 0; i < game::offsets::kMaxPlayers; ++i) {
                const uintptr_t player = context.players + game::offsets::kPlayerIndex +
                                         static_cast<uintptr_t>(i) * game::offsets::kPlayerStride;
                int index = -1;
                int type = -1;
                uint8_t active = 0;
                util::SafeRead(player + game::offsets::kPlayerIndex, index);
                util::SafeRead(player + game::offsets::kPlayerType, type);
                util::SafeRead(player + game::offsets::kPlayerActive, active);
                logger::Info("Diag: player[%u] base=0x%08X index=%d type=%d active=%u", i,
                             static_cast<unsigned>(player), index, type, active);
            }
        }
    }
    if (context.player == 0 || context.player_index < 0) {
        snapshot_.valid = false;
        previous_player_ = -1;
        return;
    }

    // The std::map at player+0x114 must at least have a valid end-node. This
    // catches the short window during map loading when the player object exists
    // but the inventory has not been constructed yet.
    uintptr_t end_node = 0;
    if (!util::SafeRead(context.player + game::offsets::kPlayerStock + 0xC, end_node) || end_node == 0) {
        if (now - last_diag > 2000) {
            logger::Info("Diag: stock map unavailable (player=0x%08X end=0x%08X)",
                         static_cast<unsigned>(context.player + game::offsets::kPlayerStock),
                         static_cast<unsigned>(end_node));
        }
        snapshot_.valid = false;
        return;
    }

    std::vector<int> amounts(DefinitionCount(), 0);
    for (size_t i = 0; i < DefinitionCount(); ++i) {
        const int amount = ReadStockAmount(context.player, static_cast<int>(kDefinitions[i].id));
        if (amount == INT_MIN) {
            logger::Warn("Good lookup failed: player=%d good=%s", context.player_index, kDefinitions[i].key);
            snapshot_.valid = false;
            return;
        }
        amounts[i] = amount;
    }

    const bool first_sample =
        previous_player_ != context.player_index || previous_amounts_.size() != amounts.size();
    if (first_sample) {
        previous_amounts_.assign(amounts.size(), 0);
        previous_player_ = context.player_index;
        snapshot_.valid = false; // no delta on the first sample
        logger::Info("Inventory context: player=%d stock=0x%08X", context.player_index,
                     static_cast<unsigned>(context.player + game::offsets::kPlayerStock));
    }

    snapshot_.values.resize(DefinitionCount());
    for (size_t i = 0; i < DefinitionCount(); ++i) {
        snapshot_.values[i].def = &kDefinitions[i];
        snapshot_.values[i].amount = amounts[i];
        snapshot_.values[i].delta = first_sample ? 0 : amounts[i] - previous_amounts_[i];
    }
    snapshot_.player_index = context.player_index;
    snapshot_.sample_time = now;
    snapshot_.valid = true;

    if (previous_amounts_ != amounts) {
        // Logging every change is useful during bring-up; the release build can
        // lower this to a periodic summary.
        char line[2048] = {};
        int used = 0;
        for (size_t i = 0; i < DefinitionCount() && used < static_cast<int>(sizeof(line)) - 64; ++i) {
            used += _snprintf_s(line + used, sizeof(line) - used, _TRUNCATE, "%s=%d ", kDefinitions[i].key,
                                amounts[i]);
        }
        logger::Info("Inventory: %s", line);
    }
    previous_amounts_ = std::move(amounts);
}

} // namespace inventory
