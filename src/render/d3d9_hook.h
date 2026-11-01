#pragma once

namespace d3d9_hook {

bool Install();
void Uninstall();
bool IsInstalled();
void* CurrentDevice();

} // namespace d3d9_hook
