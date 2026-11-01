#pragma once

#include <string>

namespace logger {

bool Init(const std::wstring& path);
void Shutdown();
void Info(const char* format, ...);
void Warn(const char* format, ...);
void Error(const char* format, ...);

} // namespace logger
