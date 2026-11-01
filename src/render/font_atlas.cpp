#include "render/font_atlas.h"

#include "common/logger.h"

#include <algorithm>
#include <cstring>

namespace render {
namespace {

int NextPowerOfTwo(int value)
{
    int result = 512;
    while (result < value) {
        result *= 2;
    }
    return result;
}

} // namespace

bool FontAtlas::Create(IDirect3DDevice9* device, int pixel_size)
{
    Release();
    if (device == nullptr) {
        return false;
    }

    pixel_size_ = std::clamp(pixel_size, 8, 48);
    const int columns = 10;
    const int cell = std::max(pixel_size_ * 2, 16);
    const int atlas_size = NextPowerOfTwo(columns * cell);

    HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        return false;
    }
    HDC memory = CreateCompatibleDC(screen);
    if (memory == nullptr) {
        ReleaseDC(nullptr, screen);
        return false;
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = atlas_size;
    info.bmiHeader.biHeight = -atlas_size; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return false;
    }
    HGDIOBJ old_bitmap = SelectObject(memory, bitmap);

    HFONT font = CreateFontW(-pixel_size_, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Tahoma");
    if (font == nullptr) {
        font = CreateFontW(-pixel_size_, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    if (font == nullptr) {
        font = CreateFontW(-pixel_size_, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    }
    HGDIOBJ old_font = SelectObject(memory, font);

    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, RGB(255, 255, 255));

    TEXTMETRICW metrics = {};
    GetTextMetricsW(memory, &metrics);
    ascent_ = metrics.tmAscent;
    descent_ = metrics.tmDescent;

    for (int c = 32; c < 127; ++c) {
        const int index = c - 32;
        const int x = (index % columns) * cell;
        const int y = (index / columns) * cell;
        const wchar_t character = static_cast<wchar_t>(c);
        SIZE size = {};
        GetTextExtentPoint32W(memory, &character, 1, &size);
        TextOutW(memory, x, y, &character, 1);
        glyphs_[c].advance = size.cx;
        glyphs_[c].ink_x = x;
        glyphs_[c].ink_y = y;
    }

    auto* pixels = static_cast<DWORD*>(bits);
    for (int c = 32; c < 127; ++c) {
        const int index = c - 32;
        const int cell_x = (index % columns) * cell;
        const int cell_y = (index / columns) * cell;
        int min_x = cell;
        int min_y = cell;
        int max_x = -1;
        int max_y = -1;
        for (int y = 0; y < cell; ++y) {
            for (int x = 0; x < cell; ++x) {
                const int px = cell_x + x;
                const int py = cell_y + y;
                const DWORD value = pixels[py * atlas_size + px];
                const BYTE b = static_cast<BYTE>(value & 0xFF);
                const BYTE g = static_cast<BYTE>((value >> 8) & 0xFF);
                const BYTE r = static_cast<BYTE>((value >> 16) & 0xFF);
                const BYTE alpha = std::max(r, std::max(g, b));
                pixels[py * atlas_size + px] = (static_cast<DWORD>(alpha) << 24) | 0x00FFFFFFu;
                if (alpha > 8) {
                    min_x = std::min(min_x, x);
                    min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x);
                    max_y = std::max(max_y, y);
                }
            }
        }

        Glyph& glyph = glyphs_[c];
        glyph.ink_x = 0;
        glyph.ink_y = 0;
        glyph.ink_w = 0;
        glyph.ink_h = 0;
        if (max_x >= min_x && max_y >= min_y) {
            glyph.ink_x = min_x;
            glyph.ink_y = min_y;
            glyph.ink_w = max_x - min_x + 1;
            glyph.ink_h = max_y - min_y + 1;
            glyph.u0 = static_cast<float>(cell_x + min_x) / static_cast<float>(atlas_size);
            glyph.v0 = static_cast<float>(cell_y + min_y) / static_cast<float>(atlas_size);
            glyph.u1 = static_cast<float>(cell_x + max_x + 1) / static_cast<float>(atlas_size);
            glyph.v1 = static_cast<float>(cell_y + max_y + 1) / static_cast<float>(atlas_size);
        }
    }

    bool ok = false;
    if (SUCCEEDED(device->CreateTexture(atlas_size, atlas_size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                        &texture_, nullptr))) {
        D3DLOCKED_RECT locked = {};
        if (SUCCEEDED(texture_->LockRect(0, &locked, nullptr, 0))) {
            for (int y = 0; y < atlas_size; ++y) {
                memcpy(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch,
                       static_cast<const BYTE*>(bits) + y * atlas_size * 4,
                       static_cast<size_t>(atlas_size) * 4);
            }
            texture_->UnlockRect(0);
            ok = true;
        } else {
            logger::Error("FontAtlas: LockRect failed");
        }
    } else {
        logger::Error("FontAtlas: CreateTexture(%d) failed", atlas_size);
    }

    SelectObject(memory, old_font);
    if (font != nullptr) {
        DeleteObject(font);
    }
    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    if (!ok) {
        Release();
        return false;
    }

    logger::Info("FontAtlas: %dpx, atlas %d, ascent=%d descent=%d", pixel_size_, atlas_size, ascent_, descent_);
    return true;
}

void FontAtlas::Release()
{
    if (texture_ != nullptr) {
        texture_->Release();
        texture_ = nullptr;
    }
    pixel_size_ = 0;
    ascent_ = 0;
    descent_ = 0;
    memset(glyphs_, 0, sizeof(glyphs_));
}

const Glyph* FontAtlas::Get(char c) const
{
    const unsigned char index = static_cast<unsigned char>(c);
    if (index >= 128) {
        return nullptr;
    }
    return &glyphs_[index];
}

int FontAtlas::Measure(const char* text) const
{
    if (text == nullptr) {
        return 0;
    }
    int width = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        const Glyph* glyph = Get(*p);
        if (glyph != nullptr) {
            width += glyph->advance;
        }
    }
    return width;
}

} // namespace render
