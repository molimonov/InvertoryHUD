#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace inventory {

struct ResourceDef {
    const char* key;
    uint32_t id;
    int icon_x;
    int icon_y;
    const char* name;
};

struct ResourceValue {
    const ResourceDef* def = nullptr;
    int amount = 0;
    int delta = 0;
};

struct Snapshot {
    bool valid = false;
    int player_index = -1;
    uint64_t sample_time = 0;
    std::vector<ResourceValue> values;

    const ResourceValue* Find(const std::string& key) const;
};

const ResourceDef* Definitions();
size_t DefinitionCount();
const ResourceDef* FindDefinition(const std::string& key);

class Reader {
public:
    void SetCompatible(bool compatible);
    void Update(bool force = false);
    const Snapshot& GetSnapshot() const { return snapshot_; }

private:
    bool ReadAmounts(uintptr_t player, std::vector<int>& amounts);
    Snapshot snapshot_;
    std::vector<int> previous_amounts_;
    bool compatible_ = false;
    int previous_player_ = -1;
    uint64_t last_update_ = 0;
};

} // namespace inventory
