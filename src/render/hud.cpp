#include "render/hud.h"

#include "common/logger.h"
#include "common/util.h"
#include "config/config.h"
#include "game/game.h"
#include "game/inventory.h"
#include "input/mouse_hook.h"
#include "render/font_atlas.h"
#include "render/game_texture.h"

#include <Windows.h>
#include <windowsx.h>
#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace hud {
namespace {

constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct Vertex {
    float x;
    float y;
    float z;
    float rhw;
    D3DCOLOR color;
    float u;
    float v;
};

float Clamp(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

struct FindWindowData {
    DWORD process_id = 0;
    HWND best = nullptr;
    LONG best_area = 0;
};

bool IsProxyWindow(HWND hwnd)
{
    wchar_t class_name[128] = {};
    wchar_t title[256] = {};
    GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name)));
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    return _wcsicmp(class_name, L"D3DProxyWindow") == 0 || _wcsicmp(title, L"D3DProxyWindow") == 0;
}

BOOL CALLBACK FindWindowCallback(HWND hwnd, LPARAM lparam)
{
    auto* data = reinterpret_cast<FindWindowData*>(lparam);
    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id != data->process_id || !IsWindowVisible(hwnd) || IsProxyWindow(hwnd)) {
        return TRUE;
    }
    if (GetWindowTextLengthW(hwnd) == 0) {
        return TRUE;
    }
    RECT client = {};
    if (!GetClientRect(hwnd, &client)) {
        return TRUE;
    }
    const LONG area = (client.right - client.left) * (client.bottom - client.top);
    if (area > data->best_area) {
        data->best = hwnd;
        data->best_area = area;
    }
    return TRUE;
}

class HudRenderer {
public:
    struct SettingsRow {
        const inventory::ResourceDef* def = nullptr;
        RECT rect = {};
    };

    void SetModule(HMODULE module)
    {
        module_ = module;
        const std::wstring directory = util::GetModuleDirectory(module);
        config_path_ = util::JoinPath(directory, L"InvertoryHUD.ini");
        config_.Load(config_path_);
        reader_.SetCompatible(game::IsSupported());
        FindGameWindow();
        InstallWindowProc();
        mouse_hook::Start(module);
    }

    void ReloadConfig()
    {
        if (!config_path_.empty()) {
            config_.Load(config_path_);
        }
    }

    void FindGameWindow()
    {
        FindWindowData data = {};
        data.process_id = GetCurrentProcessId();
        EnumWindows(FindWindowCallback, reinterpret_cast<LPARAM>(&data));

        // The D3D proxy window is created by the graphics layer and is not the
        // window that receives the player's mouse input. When the real game
        // window is in the foreground, prefer it.
        HWND foreground = GetForegroundWindow();
        if (foreground != nullptr && !IsProxyWindow(foreground)) {
            DWORD foreground_pid = 0;
            GetWindowThreadProcessId(foreground, &foreground_pid);
            if (foreground_pid == data.process_id && IsWindowVisible(foreground)) {
                RECT client = {};
                if (GetClientRect(foreground, &client)) {
                    data.best = foreground;
                    data.best_area = (client.right - client.left) * (client.bottom - client.top);
                }
            }
        }

        if (data.best != nullptr && data.best != game_hwnd_) {
            RestoreWindowProc();
            game_hwnd_ = data.best;
            client_rect_ = {};
            wchar_t title[256] = {};
            GetWindowTextW(game_hwnd_, title, static_cast<int>(std::size(title)));
            logger::Info("HUD window found: hwnd=0x%08X title=%s area=%ld",
                         static_cast<unsigned>(reinterpret_cast<uintptr_t>(game_hwnd_)),
                         util::WideToUtf8(title).c_str(), data.best_area);
        }
    }

    void InstallWindowProc()
    {
        if (game_hwnd_ == nullptr || original_wndproc_ != nullptr) {
            return;
        }
        original_wndproc_ = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(game_hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HudRenderer::WindowProcStatic)));
        if (original_wndproc_ != nullptr) {
            logger::Info("HUD window subclassed: hwnd=0x%08X", static_cast<unsigned>(reinterpret_cast<uintptr_t>(game_hwnd_)));
        } else {
            logger::Warn("SetWindowLongPtrW(GWLP_WNDPROC) failed: %lu", GetLastError());
        }
    }

    void RestoreWindowProc()
    {
        if (game_hwnd_ != nullptr && original_wndproc_ != nullptr) {
            SetWindowLongPtrW(game_hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wndproc_));
            original_wndproc_ = nullptr;
        }
    }

    static LRESULT CALLBACK WindowProcStatic(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    LRESULT WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (hwnd != game_hwnd_ || original_wndproc_ == nullptr) {
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK: {
            POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            point = ClientToHud(point);
            if (IsPointOverHud(point)) {
                return 0;
            }
            break;
        }
        default:
            break;
        }
        return CallWindowProcW(original_wndproc_, hwnd, message, wparam, lparam);
    }

    POINT ClientToHud(POINT point) const
    {
        const LONG client_width = client_rect_.right - client_rect_.left;
        const LONG client_height = client_rect_.bottom - client_rect_.top;
        if (client_width > 0 && client_height > 0 && viewport_.Width > 0 && viewport_.Height > 0) {
            point.x = static_cast<LONG>(viewport_.X) +
                      (point.x - client_rect_.left) * static_cast<LONG>(viewport_.Width) / client_width;
            point.y = static_cast<LONG>(viewport_.Y) +
                      (point.y - client_rect_.top) * static_cast<LONG>(viewport_.Height) / client_height;
        }
        return point;
    }

    bool IsPointOverHud(POINT point) const
    {
        if (PtInRect(&panel_rect_, point) != 0) {
            return true;
        }
        return settings_open_ && PtInRect(&settings_rect_, point) != 0;
    }

    void UpdateMouse()
    {
        static uint64_t last_input_log = 0;
        const uint64_t now = util::NowMs();
        if (game_hwnd_ == nullptr || !IsWindow(game_hwnd_)) {
            mouse_down_ = false;
            return;
        }
        HWND foreground = GetForegroundWindow();
        if (foreground != game_hwnd_ && GetAncestor(foreground, GA_ROOT) != game_hwnd_) {
            if (now - last_input_log > 2000) {
                last_input_log = now;
                logger::Info("Input diag: not foreground (hwnd=0x%08X fg=0x%08X)",
                             static_cast<unsigned>(reinterpret_cast<uintptr_t>(game_hwnd_)),
                             static_cast<unsigned>(reinterpret_cast<uintptr_t>(foreground)));
            }
            mouse_down_ = false;
            return;
        }

        RECT client = {};
        if (GetClientRect(game_hwnd_, &client)) {
            client_rect_ = client;
        }
        POINT point = {};
        if (!GetCursorPos(&point) || !ScreenToClient(game_hwnd_, &point)) {
            mouse_down_ = false;
            return;
        }
        point = ClientToHud(point);
        mouse_pos_ = point;

        const bool over = IsPointOverHud(point);
        if (now - last_input_log > 2000) {
            last_input_log = now;
            logger::Info("Input diag: hwnd=0x%08X fg=0x%08X point=%d,%d over=%d panel=%d,%d,%d,%d gear=%d,%d,%d,%d settings=%d",
                         static_cast<unsigned>(reinterpret_cast<uintptr_t>(game_hwnd_)),
                         static_cast<unsigned>(reinterpret_cast<uintptr_t>(foreground)), point.x, point.y,
                         over ? 1 : 0, panel_rect_.left, panel_rect_.top, panel_rect_.right, panel_rect_.bottom,
                         gear_rect_.left, gear_rect_.top, gear_rect_.right, gear_rect_.bottom,
                         settings_open_ ? 1 : 0);
        }

        const SHORT mouse_state = GetAsyncKeyState(VK_LBUTTON);
        const bool down = (mouse_state & 0x8000) != 0;
        const bool pressed = (mouse_state & 0x0001) != 0;
        if (!mouse_hook::IsActive() && (pressed || (down && !mouse_down_)) && over) {
            logger::Info("Input click: point=%d,%d settings=%d", point.x, point.y, settings_open_ ? 1 : 0);
            HandleClick(point.x, point.y);
        }
        mouse_down_ = down;
    }

    bool IsResourceVisible(const std::string& key) const
    {
        return std::find(config_.visible_resources.begin(), config_.visible_resources.end(), key) !=
               config_.visible_resources.end();
    }

    void ToggleResource(const std::string& key)
    {
        const auto it = std::find(config_.visible_resources.begin(), config_.visible_resources.end(), key);
        if (it == config_.visible_resources.end()) {
            config_.visible_resources.push_back(key);
        } else {
            config_.visible_resources.erase(it);
        }
        SaveConfig();
    }

    void SaveConfig()
    {
        config_.SaveDefaults(config_path_);
        logger::Info("HUD settings saved: %zu resources, changes=%d", config_.visible_resources.size(),
                     config_.show_changes ? 1 : 0);
    }

    void HandleClick(int x, int y)
    {
        const POINT point = {x, y};
        if (!settings_open_) {
            if (PtInRect(&gear_rect_, point) != 0) {
                settings_open_ = true;
                logger::Info("HUD settings opened by gear click");
            } else {
                logger::Info("HUD click outside gear: %d,%d gear=%d,%d,%d,%d", x, y, gear_rect_.left,
                             gear_rect_.top, gear_rect_.right, gear_rect_.bottom);
            }
            return;
        }

        if (PtInRect(&close_rect_, point) != 0 || PtInRect(&gear_rect_, point) != 0) {
            settings_open_ = false;
            logger::Info("HUD settings closed");
            return;
        }
        if (PtInRect(&show_changes_rect_, point) != 0) {
            config_.show_changes = !config_.show_changes;
            SaveConfig();
            return;
        }
        if (PtInRect(&all_rect_, point) != 0) {
            config_.visible_resources.clear();
            for (size_t i = 0; i < inventory::DefinitionCount(); ++i) {
                config_.visible_resources.push_back(inventory::Definitions()[i].key);
            }
            SaveConfig();
            return;
        }
        if (PtInRect(&none_rect_, point) != 0) {
            config_.visible_resources.clear();
            SaveConfig();
            return;
        }
        for (const SettingsRow& row : settings_rows_) {
            if (PtInRect(&row.rect, point) != 0) {
                ToggleResource(row.def->key);
                return;
            }
        }
    }

    void OnPresent(IDirect3DDevice9* device)
    {
        if (device == nullptr) {
            return;
        }
        if (device_ != device) {
            ReleaseResources();
            device_ = device;
        }
        if (!logged_present_) {
            logger::Info("First Present: device=0x%08X", static_cast<unsigned>(reinterpret_cast<uintptr_t>(device)));
            logged_present_ = true;
        }
        const uint64_t window_now = util::NowMs();
        if (game_hwnd_ == nullptr || !IsWindow(game_hwnd_) || window_now - last_window_check_ > 2000) {
            last_window_check_ = window_now;
            FindGameWindow();
            InstallWindowProc();
        }
        UpdateMouse();
        int click_x = 0;
        int click_y = 0;
        if (mouse_hook::ConsumeClick(click_x, click_y)) {
            logger::Info("HUD hook click: %d,%d settings=%d", click_x, click_y, settings_open_ ? 1 : 0);
            HandleClick(click_x, click_y);
        }

        if (GetAsyncKeyState(VK_F10) & 1) {
            ReloadConfig();
        }

        reader_.SetCompatible(game::IsSupported());
        reader_.Update();

        const inventory::Snapshot& snapshot = reader_.GetSnapshot();
        if (snapshot.valid != last_valid_ || snapshot.player_index != last_player_) {
            if (snapshot.valid) {
                logger::Info("HUD active: player=%d resources=%zu", snapshot.player_index,
                             snapshot.values.size());
            } else {
                logger::Info("HUD inactive (no local player / no inventory)");
            }
            last_valid_ = snapshot.valid;
            last_player_ = snapshot.player_index;
        }

        if (!config_.enabled || !snapshot.valid) {
            return;
        }

        Draw(device, snapshot);
    }

    void OnDeviceLost()
    {
        ReleaseResources();
    }

    void OnDeviceReset(IDirect3DDevice9* device, HRESULT result)
    {
        if (SUCCEEDED(result)) {
            device_ = device;
            ReleaseResources();
        } else {
            device_ = nullptr;
            ReleaseResources();
        }
    }

    void Shutdown()
    {
        mouse_hook::Stop();
        RestoreWindowProc();
        ReleaseResources();
        reader_.SetCompatible(false);
        device_ = nullptr;
    }

private:
    void Draw(IDirect3DDevice9* device, const inventory::Snapshot& snapshot)
    {
        D3DVIEWPORT9 viewport = {};
        if (FAILED(device->GetViewport(&viewport)) || viewport.Width == 0 || viewport.Height == 0) {
            return;
        }

        viewport_ = viewport;
        const float scale_x = static_cast<float>(viewport.Width) / 1024.0f;
        const float scale_y = static_cast<float>(viewport.Height) / 768.0f;
        const float scale = Clamp(std::min(scale_x, scale_y), 0.70f, 2.50f);

        const bool trace = frame_count_ < 2;
        if (trace) {
            logger::Info("HUD draw: viewport=%ux%u scale=%.2f", viewport.Width, viewport.Height, scale);
        }
        if (!EnsureResources(device, scale)) {
            return;
        }

        std::vector<const inventory::ResourceDef*> visible;
        for (const std::string& key : config_.visible_resources) {
            if (const inventory::ResourceDef* def = inventory::FindDefinition(key)) {
                visible.push_back(def);
            }
        }
        if (visible.empty()) {
            for (const std::string& key : DefaultVisibleResources()) {
                if (const inventory::ResourceDef* def = inventory::FindDefinition(key)) {
                    visible.push_back(def);
                }
            }
        }
        const int icon_size = std::max(10, static_cast<int>(std::lround(config_.icon_size * scale)));
        const int padding = std::max(3, static_cast<int>(std::lround(4.0f * scale)));
        const int gap = std::max(2, static_cast<int>(std::lround(3.0f * scale)));
        const int text_width = std::max(26, static_cast<int>(std::lround(44.0f * scale)));
        const int item_width = icon_size + gap + text_width;
        const int button_size = icon_size;
        const int panel_width = padding * 2 + static_cast<int>(visible.size()) * item_width + gap + button_size;
        const int panel_height = padding * 2 + std::max(icon_size, button_size);

        const int margin_x = static_cast<int>(std::lround(config_.x * scale));
        const int margin_y = static_cast<int>(std::lround(config_.y * scale));
        int panel_x = config_.anchor_center
                          ? (static_cast<int>(viewport.Width) - panel_width) / 2
                          : (config_.anchor_right ? static_cast<int>(viewport.Width) - panel_width - margin_x
                                                  : margin_x);
        int panel_y = config_.anchor_bottom ? static_cast<int>(viewport.Height) - panel_height - margin_y
                                            : margin_y;
        panel_x = std::max(0, panel_x);
        panel_y = std::max(0, panel_y);
        if (panel_x + panel_width > static_cast<int>(viewport.Width)) {
            panel_x = std::max(0, static_cast<int>(viewport.Width) - panel_width);
        }
        if (panel_y + panel_height > static_cast<int>(viewport.Height)) {
            panel_y = std::max(0, static_cast<int>(viewport.Height) - panel_height);
        }
        panel_rect_ = {panel_x, panel_y, panel_x + panel_width, panel_y + panel_height};
        gear_rect_ = {panel_x + panel_width - padding - button_size,
                      panel_y + (panel_height - button_size) / 2,
                      panel_x + panel_width - padding,
                      panel_y + (panel_height - button_size) / 2 + button_size};
        if (trace) {
            logger::Info("HUD draw: items=%zu panel=%d,%d %dx%d icon=%d font=%d", visible.size(), panel_x,
                         panel_y, panel_width, panel_height, icon_size, font_pixel_size_);
        }

        if (state_block_ == nullptr) {
            return;
        }
        if (FAILED(state_block_->Capture())) {
            logger::Warn("StateBlock::Capture failed");
            return;
        }
        if (trace) {
            logger::Info("HUD draw: state block captured");
        }

        device->SetVertexShader(nullptr);
        device->SetPixelShader(nullptr);
        device->SetVertexDeclaration(nullptr);
        device->SetStreamSource(0, nullptr, 0, 0);
        device->SetIndices(nullptr);
        device->SetFVF(kFvf);

        device->SetRenderState(D3DRS_ZENABLE, FALSE);
        device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
        device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        device->SetRenderState(D3DRS_FOGENABLE, FALSE);
        device->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
        device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
        device->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
        device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);
        device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
        device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        device->SetRenderState(D3DRS_DITHERENABLE, FALSE);
        device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
        device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0000000F); // R | G | B | A

        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        for (DWORD stage = 1; stage < 8; ++stage) {
            device->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_DISABLE);
            device->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            device->SetTexture(stage, nullptr);
        }
        device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

        const int border_alpha = std::min(255, config_.background_alpha + 40);
        DrawQuad(device, static_cast<float>(panel_x - 1), static_cast<float>(panel_y - 1),
                 static_cast<float>(panel_width + 2), static_cast<float>(panel_height + 2),
                 D3DCOLOR_ARGB(border_alpha, 0, 0, 0), white_texture_, 0.0f, 0.0f, 1.0f, 1.0f);
        DrawQuad(device, static_cast<float>(panel_x), static_cast<float>(panel_y),
                 static_cast<float>(panel_width), static_cast<float>(panel_height),
                 D3DCOLOR_ARGB(config_.background_alpha, 18, 22, 28), white_texture_, 0.0f, 0.0f, 1.0f, 1.0f);
        if (trace) {
            logger::Info("HUD draw: background done");
        }

        const int icon_y = panel_y + padding;

        // First pass: all icons. D3DXFont changes device state internally, so
        // text must not be interleaved with icon quads.
        for (size_t i = 0; i < visible.size(); ++i) {
            const int item_x = panel_x + padding + static_cast<int>(i) * item_width;
            if (trace && i == 0) {
                logger::Info("HUD draw: first item key=%s icon_tex=%p", visible[i]->key,
                             static_cast<void*>(icon_texture_));
            }

            DrawResourceIcon(device, visible[i], item_x, icon_y, icon_size);
        }
        if (trace) {
            logger::Info("HUD draw: icons done");
        }
        DrawGear(device, gear_rect_);

        // Second pass: counts and changes.
        for (size_t i = 0; i < visible.size(); ++i) {
            const int item_x = panel_x + padding + static_cast<int>(i) * item_width;
            const inventory::ResourceValue* value = snapshot.Find(visible[i]->key);
            if (value == nullptr) {
                continue;
            }

            char amount_text[32] = {};
            _snprintf_s(amount_text, sizeof(amount_text), _TRUNCATE, "%d", value->amount);
            const int text_x = item_x + icon_size + gap;
            const int text_top = icon_y;
            const int text_height = icon_size;

            if (font_atlas_.Valid()) {
                if (trace && i == 0) {
                    logger::Info("HUD draw: first text '%s'", amount_text);
                }
                RECT amount_rect = {text_x, text_top, text_x + text_width, text_top + text_height};
                DrawTextShadowed(device, amount_rect, amount_text, D3DCOLOR_ARGB(255, 255, 255, 255));
                if (trace && i == 0) {
                    logger::Info("HUD draw: first text done");
                }

                if (config_.show_changes && value->delta != 0) {
                    char delta_text[32] = {};
                    _snprintf_s(delta_text, sizeof(delta_text), _TRUNCATE, "%+d", value->delta);
                    const int amount_width = MeasureText(amount_text);
                    RECT delta_rect = {text_x + amount_width + std::max(2, gap / 2), text_top,
                                       text_x + text_width + 24, text_top + text_height};
                    const D3DCOLOR color = value->delta > 0 ? D3DCOLOR_ARGB(255, 120, 255, 120)
                                                            : D3DCOLOR_ARGB(255, 255, 110, 110);
                    DrawTextShadowed(device, delta_rect, delta_text, color);
                }
            }
        }

        if (settings_open_) {
            DrawSettings(device, scale, gap);
        }

        if (game_hwnd_ != nullptr && IsWindow(game_hwnd_)) {
            GetClientRect(game_hwnd_, &client_rect_);
            mouse_hook::UpdateRects(game_hwnd_, client_rect_, static_cast<int>(viewport_.X),
                                    static_cast<int>(viewport_.Y), static_cast<int>(viewport_.Width),
                                    static_cast<int>(viewport_.Height), panel_rect_, settings_rect_,
                                    settings_open_);
        }

        if (trace) {
            logger::Info("HUD draw: items done, applying state block");
        }
        state_block_->Apply();
        if (trace) {
            logger::Info("HUD draw: complete");
        }
        ++frame_count_;
    }

    bool EnsureResources(IDirect3DDevice9* device, float scale)
    {
        if (state_block_ == nullptr) {
            if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &state_block_))) {
                logger::Error("CreateStateBlock failed");
                state_block_ = nullptr;
                return false;
            }
            logger::Info("HUD: state block created");
        }

        if (white_texture_ == nullptr) {
            if (FAILED(device->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &white_texture_,
                                             nullptr))) {
                logger::Error("CreateTexture(white) failed");
                return false;
            }
            D3DLOCKED_RECT locked = {};
            if (FAILED(white_texture_->LockRect(0, &locked, nullptr, 0))) {
                logger::Error("LockRect(white) failed");
                return false;
            }
            *reinterpret_cast<DWORD*>(locked.pBits) = 0xFFFFFFFFu;
            white_texture_->UnlockRect(0);
            logger::Info("HUD: white texture created");
        }

        if (icon_texture_ == nullptr && !icon_load_attempted_) {
            icon_load_attempted_ = true;
            const std::wstring root = util::GetGameRoot();
            const std::wstring path = util::JoinPath(util::JoinPath(util::JoinPath(root, L"data"), L"gfx"),
                                                     L"uiElements1.dds");
            logger::Info("HUD: loading icon atlas %s", util::WideToUtf8(path).c_str());
            icon_texture_ = render::LoadGameTexture(device, path, "uiElements1.dds");
            if (icon_texture_ != nullptr) {
                D3DSURFACE_DESC desc = {};
                if (SUCCEEDED(icon_texture_->GetLevelDesc(0, &desc))) {
                    icon_texture_width_ = static_cast<int>(desc.Width);
                    icon_texture_height_ = static_cast<int>(desc.Height);
                }
                logger::Info("HUD: icon atlas %dx%d", icon_texture_width_, icon_texture_height_);
            } else {
                logger::Warn("Goods icon atlas unavailable; using colored placeholders");
            }
        }

        const int requested_font = std::max(8, static_cast<int>(std::lround(config_.font_size * scale)));
        if (font_pixel_size_ != requested_font) {
            font_atlas_.Release();
            font_pixel_size_ = requested_font;
            font_atlas_.Create(device, requested_font);
        }

        return white_texture_ != nullptr;
    }

    int MeasureText(const char* text) const
    {
        return font_atlas_.Measure(text);
    }

    void DrawTextAt(IDirect3DDevice9* device, int x, int top, int height, const char* text, D3DCOLOR color)
    {
        if (!font_atlas_.Valid() || text == nullptr) {
            return;
        }
        const int line_height = font_atlas_.LineHeight();
        const int baseline = top + (height - line_height) / 2 + font_atlas_.Ascent();
        int pen = x;
        for (const char* p = text; *p != '\0'; ++p) {
            const render::Glyph* glyph = font_atlas_.Get(*p);
            if (glyph == nullptr) {
                continue;
            }
            if (glyph->ink_w > 0 && glyph->ink_h > 0) {
                DrawQuad(device, static_cast<float>(pen + glyph->ink_x),
                         static_cast<float>(baseline - font_atlas_.Ascent() + glyph->ink_y),
                         static_cast<float>(glyph->ink_w), static_cast<float>(glyph->ink_h), color,
                         font_atlas_.Texture(), glyph->u0, glyph->v0, glyph->u1, glyph->v1);
            }
            pen += glyph->advance;
        }
    }

    void DrawTextShadowed(IDirect3DDevice9* device, RECT rect, const char* text, D3DCOLOR color)
    {
        if (!font_atlas_.Valid()) {
            return;
        }
        const int height = rect.bottom - rect.top;
        DrawTextAt(device, rect.left + 1, rect.top + 1, height, text, D3DCOLOR_ARGB(190, 0, 0, 0));
        DrawTextAt(device, rect.left, rect.top, height, text, color);
    }

    void DrawResourceIcon(IDirect3DDevice9* device, const inventory::ResourceDef* def, int x, int y, int size)
    {
        if (def == nullptr) {
            return;
        }
        if (icon_texture_ != nullptr && icon_texture_width_ > 0 && icon_texture_height_ > 0) {
            const float cell = 48.0f;
            const float u0 = static_cast<float>(def->icon_x) * cell / static_cast<float>(icon_texture_width_);
            const float v0 = static_cast<float>(def->icon_y) * cell / static_cast<float>(icon_texture_height_);
            const float u1 = u0 + cell / static_cast<float>(icon_texture_width_);
            const float v1 = v0 + cell / static_cast<float>(icon_texture_height_);
            DrawQuad(device, static_cast<float>(x), static_cast<float>(y), static_cast<float>(size),
                     static_cast<float>(size), D3DCOLOR_ARGB(255, 255, 255, 255), icon_texture_, u0, v0, u1, v1);
        } else {
            const uint32_t id = def->id;
            const BYTE r = static_cast<BYTE>(80 + (id & 0x7F));
            const BYTE g = static_cast<BYTE>(80 + ((id >> 8) & 0x7F));
            const BYTE b = static_cast<BYTE>(80 + ((id >> 16) & 0x7F));
            DrawQuad(device, static_cast<float>(x), static_cast<float>(y), static_cast<float>(size),
                     static_cast<float>(size), D3DCOLOR_ARGB(255, r, g, b), white_texture_, 0.0f, 0.0f, 1.0f, 1.0f);
        }
    }

    void DrawLine(IDirect3DDevice9* device, float x0, float y0, float x1, float y1, float thickness,
                  D3DCOLOR color)
    {
        if (white_texture_ == nullptr) {
            return;
        }
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.01f) {
            return;
        }
        const float nx = -dy / length * thickness * 0.5f;
        const float ny = dx / length * thickness * 0.5f;
        const Vertex vertices[4] = {
            {x0 + nx, y0 + ny, 0.0f, 1.0f, color, 0.0f, 0.0f},
            {x1 + nx, y1 + ny, 0.0f, 1.0f, color, 1.0f, 0.0f},
            {x0 - nx, y0 - ny, 0.0f, 1.0f, color, 0.0f, 1.0f},
            {x1 - nx, y1 - ny, 0.0f, 1.0f, color, 1.0f, 1.0f},
        };
        device->SetTexture(0, white_texture_);
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex));
    }

    void DrawCheckbox(IDirect3DDevice9* device, int x, int y, int size, bool checked, float scale)
    {
        DrawQuad(device, static_cast<float>(x - 1), static_cast<float>(y - 1), static_cast<float>(size + 2),
                 static_cast<float>(size + 2), D3DCOLOR_ARGB(255, 8, 10, 14), white_texture_, 0.0f, 0.0f, 1.0f,
                 1.0f);
        DrawQuad(device, static_cast<float>(x), static_cast<float>(y), static_cast<float>(size),
                 static_cast<float>(size), D3DCOLOR_ARGB(255, 34, 42, 52), white_texture_, 0.0f, 0.0f, 1.0f, 1.0f);
        if (checked) {
            const float thickness = std::max(1.5f, 2.0f * scale);
            DrawLine(device, static_cast<float>(x + size * 0.20f), static_cast<float>(y + size * 0.55f),
                     static_cast<float>(x + size * 0.44f), static_cast<float>(y + size * 0.80f), thickness,
                     D3DCOLOR_ARGB(255, 120, 255, 120));
            DrawLine(device, static_cast<float>(x + size * 0.44f), static_cast<float>(y + size * 0.80f),
                     static_cast<float>(x + size * 0.82f), static_cast<float>(y + size * 0.24f), thickness,
                     D3DCOLOR_ARGB(255, 120, 255, 120));
        }
    }

    void DrawButton(IDirect3DDevice9* device, RECT rect, const char* text)
    {
        DrawQuad(device, static_cast<float>(rect.left - 1), static_cast<float>(rect.top - 1),
                 static_cast<float>(rect.right - rect.left + 2), static_cast<float>(rect.bottom - rect.top + 2),
                 D3DCOLOR_ARGB(255, 8, 10, 14), white_texture_, 0.0f, 0.0f, 1.0f, 1.0f);
        DrawQuad(device, static_cast<float>(rect.left), static_cast<float>(rect.top),
                 static_cast<float>(rect.right - rect.left), static_cast<float>(rect.bottom - rect.top),
                 D3DCOLOR_ARGB(255, 42, 50, 60), white_texture_, 0.0f, 0.0f, 1.0f, 1.0f);
        const int text_width = MeasureText(text);
        const int text_x = rect.left + ((rect.right - rect.left) - text_width) / 2;
        DrawTextAt(device, text_x, rect.top, rect.bottom - rect.top, text, D3DCOLOR_ARGB(255, 240, 240, 240));
    }

    void DrawGear(IDirect3DDevice9* device, RECT rect)
    {
        DrawQuad(device, static_cast<float>(rect.left), static_cast<float>(rect.top),
                 static_cast<float>(rect.right - rect.left), static_cast<float>(rect.bottom - rect.top),
                 D3DCOLOR_ARGB(190, 10, 12, 16), white_texture_, 0.0f, 0.0f, 1.0f, 1.0f);
        DrawQuad(device, static_cast<float>(rect.left), static_cast<float>(rect.top),
                 static_cast<float>(rect.right - rect.left), 1.0f, D3DCOLOR_ARGB(220, 90, 100, 110),
                 white_texture_, 0.0f, 0.0f, 1.0f, 1.0f);

        const float x = static_cast<float>(rect.left);
        const float y = static_cast<float>(rect.top);
        const float s = static_cast<float>(rect.right - rect.left);
        const D3DCOLOR color = D3DCOLOR_ARGB(255, 220, 220, 220);
        DrawQuad(device, x + s * 0.25f, y + s * 0.25f, s * 0.50f, s * 0.50f, color, white_texture_, 0.0f, 0.0f,
                 1.0f, 1.0f);
        DrawQuad(device, x + s * 0.40f, y + s * 0.05f, s * 0.20f, s * 0.25f, color, white_texture_, 0.0f, 0.0f,
                 1.0f, 1.0f);
        DrawQuad(device, x + s * 0.40f, y + s * 0.70f, s * 0.20f, s * 0.25f, color, white_texture_, 0.0f, 0.0f,
                 1.0f, 1.0f);
        DrawQuad(device, x + s * 0.05f, y + s * 0.40f, s * 0.25f, s * 0.20f, color, white_texture_, 0.0f, 0.0f,
                 1.0f, 1.0f);
        DrawQuad(device, x + s * 0.70f, y + s * 0.40f, s * 0.25f, s * 0.20f, color, white_texture_, 0.0f, 0.0f,
                 1.0f, 1.0f);
        DrawQuad(device, x + s * 0.15f, y + s * 0.15f, s * 0.15f, s * 0.15f, color, white_texture_, 0.0f, 0.0f,
                 1.0f, 1.0f);
        DrawQuad(device, x + s * 0.70f, y + s * 0.15f, s * 0.15f, s * 0.15f, color, white_texture_, 0.0f, 0.0f,
                 1.0f, 1.0f);
        DrawQuad(device, x + s * 0.15f, y + s * 0.70f, s * 0.15f, s * 0.15f, color, white_texture_, 0.0f, 0.0f,
                 1.0f, 1.0f);
        DrawQuad(device, x + s * 0.70f, y + s * 0.70f, s * 0.15f, s * 0.15f, color, white_texture_, 0.0f, 0.0f,
                 1.0f, 1.0f);
        DrawQuad(device, x + s * 0.42f, y + s * 0.42f, s * 0.16f, s * 0.16f, D3DCOLOR_ARGB(255, 20, 24, 30),
                 white_texture_, 0.0f, 0.0f, 1.0f, 1.0f);
    }

    void DrawSettings(IDirect3DDevice9* device, float scale, int gap)
    {
        const int settings_padding = std::max(6, static_cast<int>(std::lround(8.0f * scale)));
        const int header_height = std::max(18, static_cast<int>(std::lround(22.0f * scale)));
        const int row_height = std::max(14, static_cast<int>(std::lround(18.0f * scale)));
        const int checkbox = std::max(10, static_cast<int>(std::lround(11.0f * scale)));
        const int icon = std::max(12, static_cast<int>(std::lround(16.0f * scale)));
        const int column_gap = std::max(6, static_cast<int>(std::lround(10.0f * scale)));
        const int column_width = std::max(150, static_cast<int>(std::lround(190.0f * scale)));
        const int footer_height = std::max(18, static_cast<int>(std::lround(22.0f * scale)));
        const size_t count = inventory::DefinitionCount();
        const int rows_per_column = static_cast<int>((count + 1) / 2);
        const int settings_width = settings_padding * 2 + column_width * 2 + column_gap;
        const int settings_height =
            settings_padding * 2 + header_height + rows_per_column * row_height + footer_height;

        int settings_x = (static_cast<int>(viewport_.Width) - settings_width) / 2;
        int settings_y = panel_rect_.top - settings_height - gap * 2;
        if (settings_y < 0) {
            settings_y = panel_rect_.bottom + gap * 2;
        }
        if (settings_y + settings_height > static_cast<int>(viewport_.Height)) {
            settings_y = std::max(0, static_cast<int>(viewport_.Height) - settings_height);
        }
        if (settings_x + settings_width > static_cast<int>(viewport_.Width)) {
            settings_x = std::max(0, static_cast<int>(viewport_.Width) - settings_width);
        }
        settings_rect_ = {settings_x, settings_y, settings_x + settings_width, settings_y + settings_height};

        DrawQuad(device, static_cast<float>(settings_rect_.left - 1), static_cast<float>(settings_rect_.top - 1),
                 static_cast<float>(settings_width + 2), static_cast<float>(settings_height + 2),
                 D3DCOLOR_ARGB(240, 0, 0, 0), white_texture_, 0.0f, 0.0f, 1.0f, 1.0f);
        DrawQuad(device, static_cast<float>(settings_rect_.left), static_cast<float>(settings_rect_.top),
                 static_cast<float>(settings_width), static_cast<float>(settings_height),
                 D3DCOLOR_ARGB(235, 18, 22, 28), white_texture_, 0.0f, 0.0f, 1.0f, 1.0f);

        DrawTextAt(device, settings_x + settings_padding, settings_y + settings_padding, header_height, "RESOURCES",
                   D3DCOLOR_ARGB(255, 230, 230, 230));
        close_rect_ = {settings_x + settings_width - settings_padding - header_height,
                       settings_y + settings_padding, settings_x + settings_width - settings_padding,
                       settings_y + settings_padding + header_height};
        DrawLine(device, static_cast<float>(close_rect_.left + 5), static_cast<float>(close_rect_.top + 5),
                 static_cast<float>(close_rect_.right - 5), static_cast<float>(close_rect_.bottom - 5),
                 std::max(1.5f, 2.0f * scale), D3DCOLOR_ARGB(255, 230, 230, 230));
        DrawLine(device, static_cast<float>(close_rect_.right - 5), static_cast<float>(close_rect_.top + 5),
                 static_cast<float>(close_rect_.left + 5), static_cast<float>(close_rect_.bottom - 5),
                 std::max(1.5f, 2.0f * scale), D3DCOLOR_ARGB(255, 230, 230, 230));

        settings_rows_.clear();
        settings_rows_.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const inventory::ResourceDef* def = &inventory::Definitions()[i];
            const int column = static_cast<int>(i) / rows_per_column;
            const int row = static_cast<int>(i) % rows_per_column;
            const int x = settings_x + settings_padding + column * (column_width + column_gap);
            const int y = settings_y + settings_padding + header_height + row * row_height;
            RECT row_rect = {x, y, x + column_width, y + row_height};
            settings_rows_.push_back({def, row_rect});

            DrawCheckbox(device, x, y + (row_height - checkbox) / 2, checkbox, IsResourceVisible(def->key), scale);
            DrawResourceIcon(device, def, x + checkbox + gap, y + (row_height - icon) / 2, icon);
            DrawTextAt(device, x + checkbox + gap + icon + gap, y, row_height, def->name,
                       D3DCOLOR_ARGB(255, 225, 225, 225));
        }

        const int footer_y = settings_y + settings_height - settings_padding - footer_height;
        show_changes_rect_ = {settings_x + settings_padding, footer_y, settings_x + settings_padding + checkbox,
                              footer_y + checkbox};
        DrawCheckbox(device, show_changes_rect_.left, show_changes_rect_.top, checkbox, config_.show_changes, scale);
        DrawTextAt(device, show_changes_rect_.right + gap, footer_y, footer_height, "Changes",
                   D3DCOLOR_ARGB(255, 225, 225, 225));

        const int button_width = std::max(36, static_cast<int>(std::lround(44.0f * scale)));
        const int button_height = std::max(16, static_cast<int>(std::lround(20.0f * scale)));
        const int button_y = settings_y + settings_height - settings_padding - button_height;
        all_rect_ = {settings_x + settings_width - settings_padding - button_width * 2 - gap, button_y,
                     settings_x + settings_width - settings_padding - button_width - gap, button_y + button_height};
        none_rect_ = {settings_x + settings_width - settings_padding - button_width, button_y,
                      settings_x + settings_width - settings_padding, button_y + button_height};
        DrawButton(device, all_rect_, "All");
        DrawButton(device, none_rect_, "None");
    }

    void DrawQuad(IDirect3DDevice9* device, float x, float y, float width, float height, D3DCOLOR color,
                  IDirect3DTexture9* texture, float u0, float v0, float u1, float v1)
    {
        if (texture == nullptr) {
            return;
        }
        const float x1 = x + width;
        const float y1 = y + height;
        const Vertex vertices[4] = {
            {x, y, 0.0f, 1.0f, color, u0, v0},
            {x1, y, 0.0f, 1.0f, color, u1, v0},
            {x, y1, 0.0f, 1.0f, color, u0, v1},
            {x1, y1, 0.0f, 1.0f, color, u1, v1},
        };
        device->SetTexture(0, texture);
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex));
    }

    void ReleaseResources()
    {
        font_atlas_.Release();
        font_pixel_size_ = 0;

        if (icon_texture_ != nullptr) {
            icon_texture_->Release();
            icon_texture_ = nullptr;
        }
        icon_load_attempted_ = false;
        icon_texture_width_ = 0;
        icon_texture_height_ = 0;

        if (white_texture_ != nullptr) {
            white_texture_->Release();
            white_texture_ = nullptr;
        }
        if (state_block_ != nullptr) {
            state_block_->Release();
            state_block_ = nullptr;
        }
    }

    HMODULE module_ = nullptr;
    std::wstring config_path_;
    Config config_;
    inventory::Reader reader_;

    IDirect3DDevice9* device_ = nullptr;
    render::FontAtlas font_atlas_;
    int font_pixel_size_ = 0;

    IDirect3DTexture9* icon_texture_ = nullptr;
    int icon_texture_width_ = 0;
    int icon_texture_height_ = 0;
    bool icon_load_attempted_ = false;

    IDirect3DTexture9* white_texture_ = nullptr;
    IDirect3DStateBlock9* state_block_ = nullptr;

    bool settings_open_ = false;
    RECT panel_rect_ = {};
    RECT gear_rect_ = {};
    RECT settings_rect_ = {};
    RECT close_rect_ = {};
    RECT show_changes_rect_ = {};
    RECT all_rect_ = {};
    RECT none_rect_ = {};
    std::vector<SettingsRow> settings_rows_;
    HWND game_hwnd_ = nullptr;
    WNDPROC original_wndproc_ = nullptr;
    D3DVIEWPORT9 viewport_ = {};
    RECT client_rect_ = {};
    bool mouse_down_ = false;
    POINT mouse_pos_ = {-1, -1};

    bool logged_present_ = false;
    bool last_valid_ = false;
    int last_player_ = -1;
    uint64_t frame_count_ = 0;
    uint64_t last_window_check_ = 0;
};

HudRenderer g_hud;

LRESULT CALLBACK HudRenderer::WindowProcStatic(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    return g_hud.WindowProc(hwnd, message, wparam, lparam);
}

} // namespace

void SetModule(HMODULE module)
{
    g_hud.SetModule(module);
}

void OnPresent(IDirect3DDevice9* device)
{
    g_hud.OnPresent(device);
}

void OnDeviceLost()
{
    g_hud.OnDeviceLost();
}

void OnDeviceReset(IDirect3DDevice9* device, HRESULT result)
{
    g_hud.OnDeviceReset(device, result);
}

void ReloadConfig()
{
    g_hud.ReloadConfig();
}

void Shutdown()
{
    g_hud.Shutdown();
}

} // namespace hud
