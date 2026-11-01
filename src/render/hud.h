#pragma once

#include <Windows.h>
#include <d3d9.h>

namespace hud {

void SetModule(HMODULE module);
void OnPresent(IDirect3DDevice9* device);
void OnDeviceLost();
void OnDeviceReset(IDirect3DDevice9* device, HRESULT result);
void ReloadConfig();
void Shutdown();

} // namespace hud
