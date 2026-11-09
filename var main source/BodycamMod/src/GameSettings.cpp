#include "../include/GameSettings.hpp"
#include "../include/Memory.hpp"
#include "../include/Log.hpp"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>


namespace {
    std::string g_status = "Not applied yet";

    std::wstring ProjectName() {
        HANDLE h = GetRemoteProcessHandle();
        if (!h) return L"";
        wchar_t path[MAX_PATH * 2]{};
        DWORD n = MAX_PATH * 2;
        if (!QueryFullProcessImageNameW(h, 0, path, &n)) return L"";
        std::wstring p(path, n);
        for (int i = 0; i < 3; ++i) {
            const size_t s = p.find_last_of(L'\\');
            if (s == std::wstring::npos) return L"";
            p.resize(s);
        }
        const size_t s = p.find_last_of(L'\\');
        return s == std::wstring::npos ? p : p.substr(s + 1);
    }

    bool DirExists(const std::wstring& d) {
        const DWORD a = GetFileAttributesW(d.c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool EditIni(const std::wstring& file, const std::string& section, const std::string& key, const std::string& value) {
        std::vector<std::string> lines;
        {
            std::ifstream in{ std::filesystem::path(file) };
            std::string l;
            while (std::getline(in, l)) {
                if (!l.empty() && l.back() == '\r') l.pop_back();
                lines.push_back(l);
            }
        }
        const std::string head = "[" + section + "]";
        bool inSec = false, done = false, sawSec = false;
        std::vector<std::string> out;
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string& l = lines[i];
            const bool isHead = !l.empty() && l[0] == '[';
            if (isHead) {
                if (inSec && !done && !value.empty()) { out.push_back(key + "=" + value); done = true; }
                inSec = l == head;
                if (inSec) sawSec = true;
                out.push_back(l);
                continue;
            }
            if (inSec && l.compare(0, key.size() + 1, key + "=") == 0) {
                if (!value.empty() && !done) { out.push_back(key + "=" + value); done = true; }
                continue;
            }
            out.push_back(l);
        }
        if (!value.empty() && !done) {
            if (!sawSec) { if (!out.empty() && !out.back().empty()) out.push_back(""); out.push_back(head); }
            out.push_back(key + "=" + value);
        }
        std::ofstream o{ std::filesystem::path(file), std::ios::trunc };
        if (!o) return false;
        for (const std::string& l : out) o << l << "\r\n";
        return true;
    }
}

void GameSettings_Apply() {
    const std::wstring project = ProjectName();
    wchar_t local[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (project.empty() || !n || n >= MAX_PATH) { g_status = "Game folder not found"; Log_Add(Log_Warn, "Game sound: game folder not found"); return; }
    const std::wstring cfg = std::wstring(local) + L"\\" + project + L"\\Saved\\Config";
    std::vector<std::wstring> dirs;
    for (const wchar_t* sub : { L"\\Windows", L"\\WindowsClient", L"\\WindowsNoEditor" })
        if (DirExists(cfg + sub)) dirs.push_back(cfg + sub);
    if (dirs.empty()) {
        CreateDirectoryW((std::wstring(local) + L"\\" + project).c_str(), nullptr);
        CreateDirectoryW((std::wstring(local) + L"\\" + project + L"\\Saved").c_str(), nullptr);
        CreateDirectoryW(cfg.c_str(), nullptr);
        CreateDirectoryW((cfg + L"\\Windows").c_str(), nullptr);
        dirs.push_back(cfg + L"\\Windows");
    }
    int ok = 0;
    for (const std::wstring& d : dirs)
        if (EditIni(d + L"\\Engine.ini", "Audio", "UnfocusedVolumeMultiplier", "1.0")) ++ok;
    g_status = ok == 0 ? "Could not write Engine.ini"
             : "Set (restart the game once if sound still cuts out)";
    Log_Add(ok ? Log_Info : Log_Warn, "Game sound with menu open: %s", g_status.c_str());
}

