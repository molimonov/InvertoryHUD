#include "config/config.h"

#include "common/logger.h"
#include "common/util.h"
#include "game/inventory.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <iterator>

namespace {

std::string Trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    return value;
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::vector<std::string> Split(const std::string& value)
{
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t end = value.find(',', start);
        std::string item = Trim(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!item.empty()) {
            result.push_back(Lower(std::move(item)));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

std::string ReadString(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                       const wchar_t* default_value)
{
    wchar_t buffer[2048] = {};
    GetPrivateProfileStringW(section, key, default_value, buffer, static_cast<DWORD>(std::size(buffer)),
                             path.c_str());
    return util::WideToAnsi(buffer);
}

} // namespace

std::vector<std::string> DefaultVisibleResources()
{
    return {"wood", "plank", "stone", "corn", "bread"};
}

void Config::ResetToDefaults()
{
    enabled = true;
    show_changes = true;
    anchor_center = true;
    anchor_right = false;
    anchor_bottom = false;
    x = 0;
    y = 12;
    icon_size = 18;
    font_size = 11;
    background_alpha = 170;
    visible_resources = DefaultVisibleResources();
}

bool Config::Load(const std::wstring& path)
{
    ResetToDefaults();

    if (!util::FileExists(path)) {
        SaveDefaults(path);
        logger::Info("Config not found, defaults written: %s", util::WideToUtf8(path).c_str());
        return true;
    }

    enabled = GetPrivateProfileIntW(L"General", L"Enabled", 1, path.c_str()) != 0;
    show_changes = GetPrivateProfileIntW(L"General", L"ShowChanges", 1, path.c_str()) != 0;
    anchor_center = GetPrivateProfileIntW(L"Position", L"AnchorCenter", 1, path.c_str()) != 0;
    anchor_right = GetPrivateProfileIntW(L"Position", L"AnchorRight", 0, path.c_str()) != 0;
    anchor_bottom = GetPrivateProfileIntW(L"Position", L"AnchorBottom", 0, path.c_str()) != 0;

    x = GetPrivateProfileIntW(L"Position", L"X", 0, path.c_str());
    y = GetPrivateProfileIntW(L"Position", L"Y", 12, path.c_str());

    icon_size = GetPrivateProfileIntW(L"Appearance", L"IconSize", 18, path.c_str());
    font_size = GetPrivateProfileIntW(L"Appearance", L"FontSize", 11, path.c_str());
    background_alpha = GetPrivateProfileIntW(L"Appearance", L"BackgroundAlpha", 170, path.c_str());

    icon_size = std::clamp(icon_size, 12, 96);
    font_size = std::clamp(font_size, 8, 48);
    background_alpha = std::clamp(background_alpha, 0, 255);
    x = std::clamp(x, -4096, 4096);
    y = std::clamp(y, -4096, 4096);

    const std::string visible = ReadString(path, L"Resources", L"Visible",
                                           L"wood,plank,stone,corn,bread");
    visible_resources.clear();
    for (const std::string& key : Split(visible)) {
        if (inventory::FindDefinition(key) != nullptr) {
            visible_resources.push_back(key);
        } else {
            logger::Warn("Unknown resource in config: %s", key.c_str());
        }
    }
    if (visible_resources.empty()) {
        visible_resources = DefaultVisibleResources();
    }

    logger::Info("Config loaded: enabled=%d changes=%d pos=%d,%d icon=%d font=%d alpha=%d resources=%zu",
                 enabled ? 1 : 0, show_changes ? 1 : 0, x, y, icon_size, font_size, background_alpha,
                 visible_resources.size());
    return true;
}

void Config::SaveDefaults(const std::wstring& path) const
{
    WritePrivateProfileStringW(L"General", L"Enabled", enabled ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"General", L"ShowChanges", show_changes ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Position", L"AnchorCenter", anchor_center ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Position", L"AnchorRight", anchor_right ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Position", L"AnchorBottom", anchor_bottom ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Position", L"X", std::to_wstring(x).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Position", L"Y", std::to_wstring(y).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Appearance", L"IconSize", std::to_wstring(icon_size).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Appearance", L"FontSize", std::to_wstring(font_size).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Appearance", L"BackgroundAlpha", std::to_wstring(background_alpha).c_str(),
                               path.c_str());

    std::string visible;
    for (const std::string& key : visible_resources) {
        if (!visible.empty()) {
            visible += ",";
        }
        visible += key;
    }
    WritePrivateProfileStringW(L"Resources", L"Visible", util::AnsiToWide(visible).c_str(), path.c_str());
}
