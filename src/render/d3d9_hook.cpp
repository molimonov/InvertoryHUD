#include "render/d3d9_hook.h"

#include "common/logger.h"
#include "common/util.h"
#include "game/game.h"
#include "render/hud.h"

#include <d3d9.h>
#include <Windows.h>

namespace d3d9_hook {
namespace {

using PresentFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using ResetFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

PresentFn g_original_present = nullptr;
ResetFn g_original_reset = nullptr;
void** g_vtable = nullptr;
bool g_installed = false;

IDirect3DDevice9* GetGameDevice()
{
    if (!game::IsSupported()) {
        return nullptr;
    }

    static uint64_t last_wait_log = 0;
    const uint64_t now = util::NowMs();

    uintptr_t graphic_device = 0;
    if (!util::SafeRead(game::Base() + game::addr::kGraphicDevice, graphic_device)) {
        if (now - last_wait_log > 2000) {
            logger::Warn("Cannot read CGraphicDevice global at 0x%08X",
                         static_cast<unsigned>(game::Base() + game::addr::kGraphicDevice));
            last_wait_log = now;
        }
        return nullptr;
    }
    if (graphic_device == 0) {
        if (now - last_wait_log > 2000) {
            logger::Info("Waiting for CGraphicDevice...");
            last_wait_log = now;
        }
        return nullptr;
    }

    uintptr_t device = 0;
    if (!util::SafeRead(graphic_device + game::offsets::kGraphicDevice, device) || device == 0) {
        if (now - last_wait_log > 2000) {
            logger::Info("CGraphicDevice=0x%08X, waiting for D3D device at +0x%X (%s)",
                         static_cast<unsigned>(graphic_device), game::offsets::kGraphicDevice,
                         util::IsReadable(graphic_device, 0xB4) ? "readable" : "unreadable");
            last_wait_log = now;
        }
        return nullptr;
    }
    return reinterpret_cast<IDirect3DDevice9*>(device);
}

HRESULT WINAPI PresentHook(IDirect3DDevice9* device, const RECT* source_rect, const RECT* dest_rect, HWND hwnd,
                           const RGNDATA* dirty_region)
{
    static thread_local bool inside = false;
    if (!inside) {
        inside = true;
        try {
            hud::OnPresent(device);
        } catch (...) {
            logger::Error("C++ exception escaped hud::OnPresent");
        }
        inside = false;
    }
    return g_original_present != nullptr ? g_original_present(device, source_rect, dest_rect, hwnd, dirty_region)
                                         : D3DERR_INVALIDCALL;
}

HRESULT WINAPI ResetHook(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters)
{
    hud::OnDeviceLost();
    const HRESULT result = g_original_reset != nullptr ? g_original_reset(device, parameters) : D3DERR_INVALIDCALL;
    hud::OnDeviceReset(device, result);
    return result;
}

bool PatchVtable(void** vtable)
{
    if (vtable == nullptr || !util::IsReadable(reinterpret_cast<uintptr_t>(&vtable[16]), sizeof(void*) * 2)) {
        return false;
    }

    const auto present = reinterpret_cast<PresentFn>(vtable[17]);
    const auto reset = reinterpret_cast<ResetFn>(vtable[16]);
    if (present == nullptr || reset == nullptr) {
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(&vtable[16], sizeof(void*) * 2, PAGE_READWRITE, &old_protect)) {
        logger::Error("VirtualProtect(vtable) failed: %lu", GetLastError());
        return false;
    }

    g_original_present = present;
    g_original_reset = reset;
    vtable[17] = reinterpret_cast<void*>(&PresentHook);
    vtable[16] = reinterpret_cast<void*>(&ResetHook);
    g_vtable = vtable;

    DWORD ignored = 0;
    VirtualProtect(&vtable[16], sizeof(void*) * 2, old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &vtable[16], sizeof(void*) * 2);

    g_installed = true;
    logger::Info("Hooked IDirect3DDevice9: vtable=0x%08X Present=0x%08X Reset=0x%08X",
                 static_cast<unsigned>(reinterpret_cast<uintptr_t>(vtable)),
                 static_cast<unsigned>(reinterpret_cast<uintptr_t>(present)),
                 static_cast<unsigned>(reinterpret_cast<uintptr_t>(reset)));
    return true;
}

} // namespace

bool Install()
{
    IDirect3DDevice9* device = GetGameDevice();
    if (device == nullptr) {
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    if (vtable == nullptr) {
        logger::Warn("D3D device has null vtable");
        return false;
    }

    if (g_installed && g_vtable == vtable && vtable[17] == reinterpret_cast<void*>(&PresentHook)) {
        return true;
    }

    if (!PatchVtable(vtable)) {
        logger::Error("Failed to patch device vtable 0x%08X",
                      static_cast<unsigned>(reinterpret_cast<uintptr_t>(vtable)));
        return false;
    }
    return true;
}

void Uninstall()
{
    if (!g_installed || g_vtable == nullptr) {
        return;
    }

    DWORD old_protect = 0;
    if (VirtualProtect(&g_vtable[16], sizeof(void*) * 2, PAGE_READWRITE, &old_protect)) {
        if (g_vtable[17] == reinterpret_cast<void*>(&PresentHook) && g_original_present != nullptr) {
            g_vtable[17] = reinterpret_cast<void*>(g_original_present);
        }
        if (g_vtable[16] == reinterpret_cast<void*>(&ResetHook) && g_original_reset != nullptr) {
            g_vtable[16] = reinterpret_cast<void*>(g_original_reset);
        }
        DWORD ignored = 0;
        VirtualProtect(&g_vtable[16], sizeof(void*) * 2, old_protect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), &g_vtable[16], sizeof(void*) * 2);
    }

    g_vtable = nullptr;
    g_original_present = nullptr;
    g_original_reset = nullptr;
    g_installed = false;
    logger::Info("D3D9 hooks removed");
}

bool IsInstalled()
{
    return g_installed;
}

void* CurrentDevice()
{
    return GetGameDevice();
}

} // namespace d3d9_hook
