#pragma once

#include <d3d9.h>
#include <Windows.h>

namespace render {

struct Glyph {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    int ink_x = 0;
    int ink_y = 0;
    int ink_w = 0;
    int ink_h = 0;
    int advance = 0;
};

// A small GDI-generated ASCII atlas. The HUD uses its own quads for text so
// that no D3DXFont/ID3DXFont interface is involved in the render hook.
class FontAtlas {
public:
    bool Create(IDirect3DDevice9* device, int pixel_size);
    void Release();

    bool Valid() const { return texture_ != nullptr; }
    IDirect3DTexture9* Texture() const { return texture_; }
    int PixelSize() const { return pixel_size_; }
    int Ascent() const { return ascent_; }
    int Descent() const { return descent_; }
    int LineHeight() const { return ascent_ + descent_; }

    const Glyph* Get(char c) const;
    int Measure(const char* text) const;

private:
    IDirect3DTexture9* texture_ = nullptr;
    int pixel_size_ = 0;
    int ascent_ = 0;
    int descent_ = 0;
    Glyph glyphs_[128] = {};
};

} // namespace render
