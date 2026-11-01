#pragma once

#include <Windows.h>

namespace mouse_hook {

bool Start(HMODULE module);
void Stop();
bool IsActive();

struct State {
    HWND hwnd = nullptr;
    RECT client = {};
    int viewport_x = 0;
    int viewport_y = 0;
    int viewport_width = 0;
    int viewport_height = 0;
    RECT panel = {};
    RECT settings = {};
    bool settings_open = false;
};

void UpdateRects(HWND hwnd, const RECT& client, int viewport_x, int viewport_y, int viewport_width,
                 int viewport_height, const RECT& panel, const RECT& settings, bool settings_open);

bool ConsumeClick(int& x, int& y);

} // namespace mouse_hook
