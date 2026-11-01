#pragma once

#include <string>
#include <vector>

struct Config {
    bool enabled = true;
    bool show_changes = true;
    bool anchor_center = true;
    bool anchor_right = false;
    bool anchor_bottom = false;
    int x = 0;
    int y = 12;
    int icon_size = 18;
    int font_size = 11;
    int background_alpha = 170;
    std::vector<std::string> visible_resources;

    bool Load(const std::wstring& path);
    void SaveDefaults(const std::wstring& path) const;
    void ResetToDefaults();
};

std::vector<std::string> DefaultVisibleResources();
