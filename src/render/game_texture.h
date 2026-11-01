#pragma once

#include <d3d9.h>
#include <string>

#include "render/d3dx_min.h"

namespace render {

IDirect3DTexture9* LoadGameTexture(IDirect3DDevice9* device, const std::wstring& encrypted_path,
                                   const std::string& key_name);

bool LoadD3DX();
D3DXCreateFontA_t D3DXCreateFontFunction();

} // namespace render
