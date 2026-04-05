#pragma once

#include <Windows.h>
#include <string>

namespace fcitx {
extern HINSTANCE dllInstance;

void RegisterTrace(const std::string &message);

BOOL RegisterServer();
void UnregisterServer();
BOOL RegisterProfiles();
BOOL RegisterCategories();
void UnregisterCategoriesAndProfiles();
} // namespace fcitx
