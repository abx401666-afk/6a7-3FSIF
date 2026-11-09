#include "../include/Log.hpp"
#include <Windows.h>
#include <shellapi.h>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <deque>
#include <mutex>

namespace {
    std::vector<LogEntry> g_entries;
    std::mutex g_lock;
    FILE* g_file = nullptr;
    bool g_fileTried = false;
    const auto g_start = std::chrono::steady_clock::now();
    constexpr size_t kMax = 500;

    std::wstring Folder() {
        wchar_t buf[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        std::wstring dir = (n && n < MAX_PATH) ? std::wstring(buf) : std::wstring(L".");
        return dir + L"\\BodycamMod\\logs";
    }

    void OpenFile() {
        g_fileTried = true;
        const std::wstring dir = Folder();
        const std::wstring parent = dir.substr(0, dir.find_last_of(L'\\'));
        CreateDirectoryW(parent.c_str(), nullptr);
        CreateDirectoryW(dir.c_str(), nullptr);
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t name[64];
        swprintf(name, 64, L"\\%04u-%02u-%02u.log", st.wYear, st.wMonth, st.wDay);
        g_file = _wfopen((dir + name).c_str(), L"a");
    }
}

void Log_Add(int level, const char* fmt, ...) {
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    LogEntry e;
    e.time = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
    SYSTEMTIME st; GetLocalTime(&st);
    char clk[16];
    std::snprintf(clk, sizeof(clk), "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    e.clock = clk;
    e.level = level;
    e.text = text;

    std::lock_guard<std::mutex> lk(g_lock);
    if (!g_fileTried) OpenFile();
    if (g_file) {
        static const char* const lv[] = { "INFO", "WARN", "ERROR", "KILL", "HIT", "MISS" };
        std::fprintf(g_file, "%s [%s] %s\n", clk, lv[(level >= 0 && level < Log_LevelCount) ? level : 0], text);
        std::fflush(g_file);
    }
    if (g_entries.size() >= kMax) g_entries.erase(g_entries.begin(), g_entries.begin() + 50);
    g_entries.push_back(std::move(e));
}

namespace {
    struct MiniDumpExInfo { DWORD ThreadId; EXCEPTION_POINTERS* ExceptionPointers; BOOL ClientPointers; };
    using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int, MiniDumpExInfo*, void*, void*);
    volatile LONG g_inCrash = 0;
    void (*volatile g_crashHook)() = nullptr;

    void DescribeAddress(FILE* f, const char* tag, void* addr) {
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(addr), &mod) && mod) {
            GetModuleFileNameA(mod, name, MAX_PATH);
            const char* slash = std::strrchr(name, '\\');
            std::fprintf(f, "%s %p  %s+0x%llX\n", tag, addr, slash ? slash + 1 : name,
                         static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(mod)));
        } else {
            std::fprintf(f, "%s %p\n", tag, addr);
        }
    }

    void WriteCrash(const char* what, EXCEPTION_POINTERS* ep) {
        if (InterlockedExchange(&g_inCrash, 1)) return;
        SYSTEMTIME st; GetLocalTime(&st);
        const std::wstring dir = Folder();
        CreateDirectoryW(dir.substr(0, dir.find_last_of(L'\\')).c_str(), nullptr);
        CreateDirectoryW(dir.c_str(), nullptr);
        wchar_t stamp[48];
        swprintf(stamp, 48, L"%04u-%02u-%02u_%02u-%02u-%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        FILE* f = g_file;
        bool own = false;
        if (!f) { f = _wfopen((dir + L"\\crash-" + stamp + L".txt").c_str(), L"a"); own = true; }
        if (f) {
            std::fprintf(f, "%02u:%02u:%02u [ERROR] CRASH (%s) thread %lu\n", st.wHour, st.wMinute, st.wSecond, what, GetCurrentThreadId());
            if (ep && ep->ExceptionRecord) {
                const EXCEPTION_RECORD* r = ep->ExceptionRecord;
                std::fprintf(f, "  code 0x%08lX  flags 0x%lX\n", r->ExceptionCode, r->ExceptionFlags);
                DescribeAddress(f, "  at", r->ExceptionAddress);
                if ((r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || r->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && r->NumberParameters >= 2)
                    std::fprintf(f, "  %s address 0x%llX\n", r->ExceptionInformation[0] == 0 ? "read of" : r->ExceptionInformation[0] == 1 ? "write to" : "execute at",
                                 static_cast<unsigned long long>(r->ExceptionInformation[1]));
            }
            void* frames[32] = {};
            const USHORT n = CaptureStackBackTrace(0, 32, frames, nullptr);
            for (USHORT i = 0; i < n; ++i) DescribeAddress(f, "  frame", frames[i]);
            std::fflush(f);
        }

        if (HMODULE dbg = LoadLibraryW(L"dbghelp.dll")) {
            if (auto fn = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbg, "MiniDumpWriteDump"))) {
                const std::wstring path = dir + L"\\crash-" + stamp + L".dmp";
                HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    MiniDumpExInfo mei{ GetCurrentThreadId(), ep, FALSE };
                    const BOOL ok = fn(GetCurrentProcess(), GetCurrentProcessId(), h, 0x00001000   | 0x00000001  ,
                                       ep ? &mei : nullptr, nullptr, nullptr);
                    CloseHandle(h);
                    if (f) { std::fprintf(f, "  minidump %s\n", ok ? "written" : "FAILED"); std::fflush(f); }
                }
            }
        }
        if (auto hook = g_crashHook) {
            hook();
            if (f) { std::fprintf(f, "  game patches restored\n"); std::fflush(f); }
        }
        if (own && f) std::fclose(f);
    }

    LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
        WriteCrash("unhandled exception", ep);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void TerminateHook() {
        WriteCrash("std::terminate (exception escaped a thread, or a joinable thread was destroyed)", nullptr);
        std::abort();
    }
}

void Log_InstallCrashHandler() {
    SetUnhandledExceptionFilter(CrashFilter);
    std::set_terminate(TerminateHook);
}

void Log_SetCrashHook(void (*hook)()) { g_crashHook = hook; }
