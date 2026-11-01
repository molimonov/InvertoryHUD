#include "render/game_texture.h"

#include "common/logger.h"
#include "common/util.h"
#include "render/dds_decrypt.h"
#include "render/d3dx_min.h"

#include <Windows.h>

namespace render {
namespace {

HMODULE g_d3dx = nullptr;
D3DXCreateTextureFromFileInMemory_t g_create_texture = nullptr;
D3DXCreateFontA_t g_create_font = nullptr;

} // namespace

bool LoadD3DX()
{
    if (g_d3dx != nullptr && g_create_texture != nullptr) {
        return true;
    }

    const char* candidates[] = {"d3dx9_29.dll", "d3dx9_43.dll", "d3dx9_30.dll", "d3dx9_31.dll"};
    for (const char* candidate : candidates) {
        HMODULE module = GetModuleHandleA(candidate);
        if (module == nullptr) {
            module = LoadLibraryA(candidate);
        }
        if (module == nullptr) {
            continue;
        }
        const auto create_texture = reinterpret_cast<D3DXCreateTextureFromFileInMemory_t>(
            GetProcAddress(module, "D3DXCreateTextureFromFileInMemory"));
        const auto create_font =
            reinterpret_cast<D3DXCreateFontA_t>(GetProcAddress(module, "D3DXCreateFontA"));
        if (create_texture != nullptr) {
            g_d3dx = module;
            g_create_texture = create_texture;
            g_create_font = create_font;
            logger::Info("D3DX loaded from %s (font=%s)", candidate, create_font != nullptr ? "yes" : "no");
            return true;
        }
    }

    logger::Error("D3DXCreateTextureFromFileInMemory not found");
    return false;
}

D3DXCreateFontA_t D3DXCreateFontFunction()
{
    LoadD3DX();
    return g_create_font;
}

IDirect3DTexture9* LoadGameTexture(IDirect3DDevice9* device, const std::wstring& encrypted_path,
                                   const std::string& key_name)
{
    if (device == nullptr || !LoadD3DX()) {
        return nullptr;
    }

    std::vector<uint8_t> raw;
    if (!util::ReadFileBytes(encrypted_path, raw)) {
        logger::Error("Cannot read texture: %s", util::WideToUtf8(encrypted_path).c_str());
        return nullptr;
    }

    std::vector<uint8_t> plain;
    if (!dds::DecryptDng(raw, key_name, plain) || plain.empty()) {
        logger::Error("Cannot decrypt texture: %s", util::WideToUtf8(encrypted_path).c_str());
        return nullptr;
    }
    logger::Info("Texture plain header: %02X %02X %02X %02X, %zu bytes", plain.size() >= 4 ? plain[0] : 0,
                 plain.size() >= 4 ? plain[1] : 0, plain.size() >= 4 ? plain[2] : 0,
                 plain.size() >= 4 ? plain[3] : 0, plain.size());

    IDirect3DTexture9* texture = nullptr;
    const HRESULT hr = g_create_texture(device, plain.data(), static_cast<UINT>(plain.size()), &texture);
    if (FAILED(hr) || texture == nullptr) {
        logger::Error("D3DXCreateTextureFromFileInMemory failed: 0x%08X file=%s", static_cast<unsigned>(hr),
                      key_name.c_str());
        return nullptr;
    }

    logger::Info("Texture loaded: %s (%zu bytes plain)", key_name.c_str(), plain.size());
    return texture;
}

} // namespace render
