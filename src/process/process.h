#pragma once
#include <Windows.h>
#include <string>
DWORD FindProcessId(const std::wstring& exeName);
bool ProductVersionMatches(const std::wstring& image, const std::wstring& version);
