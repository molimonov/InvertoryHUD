#include "input/mouse_hook.h"

#include "common/logger.h"

#include <atomic>

namespace mouse_hook {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
State g_state;
std::atomic<bool> g_has_click = false;
std::atomic<LONG> g_click_x = 0;
std::atomic<LONG> g_click_y = 0;
std::atomic<bool> g_active = false;

HMODULE g_module = nullptr;
HANDLE g_thread = nullptr;
DWORD g_thread_id = 0;
HANDLE g_ready = nullptr;
HHOOK g_hook = nullptr;

int MapCoordinate(int value, int client_offset, int client_size, int viewport_offset, int viewport_size)
{
    if (client_size <= 0 || viewport_size <= 0) {
        return value;
    }
    return viewport_offset + (value - client_offset) * viewport_size / client_size;
}

LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM message, LPARAM lparam)
{
    if (code < 0) {
        return CallNextHookEx(nullptr, code, message, lparam);
    }
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
        break;
    default:
        return CallNextHookEx(nullptr, code, message, lparam);
    }

    const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
    if (info == nullptr) {
        return CallNextHookEx(nullptr, code, message, lparam);
    }

    bool over = false;
    int hud_x = 0;
    int hud_y = 0;

    AcquireSRWLockShared(&g_lock);
    if (g_state.hwnd != nullptr && IsWindow(g_state.hwnd)) {
        HWND under = WindowFromPoint(info->pt);
        if (under != nullptr &&
            (under == g_state.hwnd || IsChild(g_state.hwnd, under) != 0 ||
             GetAncestor(under, GA_ROOT) == g_state.hwnd)) {
            POINT point = info->pt;
            if (ScreenToClient(g_state.hwnd, &point)) {
                hud_x = MapCoordinate(point.x, g_state.client.left, g_state.client.right - g_state.client.left,
                                      g_state.viewport_x, g_state.viewport_width);
                hud_y = MapCoordinate(point.y, g_state.client.top, g_state.client.bottom - g_state.client.top,
                                      g_state.viewport_y, g_state.viewport_height);
                const POINT hud = {hud_x, hud_y};
                over = PtInRect(&g_state.panel, hud) != 0 ||
                       (g_state.settings_open && PtInRect(&g_state.settings, hud) != 0);
            }
        }
    }
    ReleaseSRWLockShared(&g_lock);

    if (!over) {
        return CallNextHookEx(nullptr, code, message, lparam);
    }

    if (message == WM_LBUTTONDOWN) {
        g_click_x.store(hud_x);
        g_click_y.store(hud_y);
        g_has_click.store(true);
    }
    return 1; // swallow the event while the cursor is over the HUD
}

DWORD WINAPI HookThread(LPVOID)
{
    g_thread_id = GetCurrentThreadId();
    g_hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, g_module, 0);
    if (g_hook == nullptr) {
        g_hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, nullptr, 0);
    }
    g_active.store(g_hook != nullptr);
    if (g_hook != nullptr) {
        logger::Info("Low-level mouse hook installed");
    } else {
        logger::Warn("SetWindowsHookExW(WH_MOUSE_LL) failed: %lu", GetLastError());
    }
    if (g_ready != nullptr) {
        SetEvent(g_ready);
    }

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_hook != nullptr) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
    g_active.store(false);
    return 0;
}

} // namespace

bool Start(HMODULE module)
{
    if (g_thread != nullptr) {
        return true;
    }
    g_module = module;
    g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_ready == nullptr) {
        return false;
    }
    g_thread = CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr);
    if (g_thread == nullptr) {
        CloseHandle(g_ready);
        g_ready = nullptr;
        return false;
    }
    WaitForSingleObject(g_ready, 2000);
    CloseHandle(g_ready);
    g_ready = nullptr;
    return g_active.load();
}

void Stop()
{
    if (g_thread == nullptr) {
        return;
    }
    PostThreadMessageW(g_thread_id, WM_QUIT, 0, 0);
    WaitForSingleObject(g_thread, 2000);
    CloseHandle(g_thread);
    g_thread = nullptr;
    g_active.store(false);
}

bool IsActive()
{
    return g_active.load();
}

void UpdateRects(HWND hwnd, const RECT& client, int viewport_x, int viewport_y, int viewport_width,
                 int viewport_height, const RECT& panel, const RECT& settings, bool settings_open)
{
    AcquireSRWLockExclusive(&g_lock);
    g_state.hwnd = hwnd;
    g_state.client = client;
    g_state.viewport_x = viewport_x;
    g_state.viewport_y = viewport_y;
    g_state.viewport_width = viewport_width;
    g_state.viewport_height = viewport_height;
    g_state.panel = panel;
    g_state.settings = settings;
    g_state.settings_open = settings_open;
    ReleaseSRWLockExclusive(&g_lock);
}

bool ConsumeClick(int& x, int& y)
{
    if (!g_has_click.exchange(false)) {
        return false;
    }
    x = static_cast<int>(g_click_x.load());
    y = static_cast<int>(g_click_y.load());
    return true;
}

} // namespace mouse_hook
