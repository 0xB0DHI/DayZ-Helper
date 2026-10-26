#include "process.h"
#include <TlHelp32.h>
#include <cwchar>
#include <vector>
#include <winver.h>

DWORD FindProcessId(const std::wstring& exeName) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do { if (_wcsicmp(entry.szExeFile, exeName.c_str()) == 0) { pid = entry.th32ProcessID; break; } }
        while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

bool ProductVersionMatches(const std::wstring& image, const std::wstring& version) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(image.c_str(), &handle);
    if (!size || size > 4u * 1024u * 1024u) return false;
    std::vector<unsigned char> bytes(size);
    if (!GetFileVersionInfoW(image.c_str(), 0, size, bytes.data())) return false;
    struct LanguageCodePage { WORD language,codepage; };
    LanguageCodePage* translations=nullptr;
    UINT length=0;
    if (!VerQueryValueW(bytes.data(),L"\\VarFileInfo\\Translation",
                        reinterpret_cast<void**>(&translations),&length) || !translations ||
        length<sizeof(LanguageCodePage)) return false;
    const auto count=length/sizeof(LanguageCodePage);
    for (UINT i=0;i<count;++i) {
        wchar_t query[96]{};
        swprintf_s(query,L"\\StringFileInfo\\%04x%04x\\ProductVersion",
                   translations[i].language,translations[i].codepage);
        wchar_t* found=nullptr;
        UINT textLength=0;
        if (VerQueryValueW(bytes.data(),query,reinterpret_cast<void**>(&found),&textLength) &&
            found && textLength>0 && version==found) return true;
    }
    return false;
}
