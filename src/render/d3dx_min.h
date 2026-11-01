#pragma once

#include <d3d9.h>
#include <Windows.h>

// Minimal D3DX9 declarations. The game already ships/loads d3dx9_29.dll, so we
// resolve only the two functions we need at runtime instead of adding a link
// dependency on a DirectX SDK that is not installed on the build machine.
struct ID3DXFont {
    virtual HRESULT QueryInterface(REFIID riid, void** object) = 0;
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
    virtual HRESULT GetDevice(IDirect3DDevice9** device) = 0;
    virtual HRESULT GetDescA(void* desc) = 0;
    virtual HRESULT GetDescW(void* desc) = 0;
    virtual BOOL GetTextMetricsA(void* metrics) = 0;
    virtual BOOL GetTextMetricsW(void* metrics) = 0;
    virtual HDC GetDC() = 0;
    virtual HRESULT GetGlyphData(void* glyph) = 0;
    virtual HRESULT PreloadCharacters(UINT first, UINT last) = 0;
    virtual HRESULT PreloadGlyphs(void* glyphs) = 0;
    virtual HRESULT PreloadTextA(void* text) = 0;
    virtual HRESULT PreloadTextW(void* text) = 0;
    virtual INT DrawTextA(void* sprite, const char* text, INT count, RECT* rect, DWORD format, D3DCOLOR color) = 0;
    virtual INT DrawTextW(void* sprite, const wchar_t* text, INT count, RECT* rect, DWORD format, D3DCOLOR color) = 0;
    virtual HRESULT OnLostDevice() = 0;
    virtual HRESULT OnResetDevice() = 0;
};

using D3DXCreateFontA_t = HRESULT(WINAPI*)(IDirect3DDevice9* device, INT height, UINT width, UINT weight,
                                          UINT mip_levels, BOOL italic, DWORD char_set,
                                          DWORD output_precision, DWORD quality, DWORD pitch_and_family,
                                          LPCSTR face_name, ID3DXFont** font);

using D3DXCreateTextureFromFileInMemory_t = HRESULT(WINAPI*)(IDirect3DDevice9* device, LPCVOID src_data,
                                                            UINT src_data_size, IDirect3DTexture9** texture);
