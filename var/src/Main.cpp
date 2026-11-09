#include "../include/Config.hpp"
#include "../include/menu/hashes.hpp"
#include "../include/Hud.hpp"
#include "../include/UiKit.hpp"
#include "../include/GameOffsets.hpp"
#include "../include/MenuInput.hpp"
#include "../include/GameSettings.hpp"
#include "../include/OverlayRenderer.hpp"
#include "../include/ESP.hpp"
#include "../include/Visibility.hpp"
#include "../include/Aimbot.hpp"
#include "../include/Memory.hpp"
#include "../include/Targets.hpp"
#include "../include/Lens.hpp"
#include "../include/Reload.hpp"
#include "../include/Ammo.hpp"
#include "../include/Misc.hpp"
#include "../include/World.hpp"
#include "../include/CombatLog.hpp"
#include "../include/Log.hpp"
#include "../include/GameSettings.hpp"
#include "../include/Skeleton.hpp"
#include <timeapi.h>

#include <imgui.h>
#include <Windows.h>
#include <algorithm>
#include <cwctype>
#include <iterator>
#include <string>

extern ESPConfig g_ESP;
extern AimbotConfig g_Aim;
extern bool g_MenuOpen;
void Menu_Render();
void Menu_RenderHud();

namespace {
    bool ContainsInsensitive(const std::wstring& value, const wchar_t* needle) {
        if (!needle || !*needle) return false;
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        std::wstring target = needle;
        std::transform(target.begin(), target.end(), target.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered.find(target) != std::wstring::npos;
    }

    HWND FindWindowForProcess(DWORD processId) {
        struct SearchContext {
            DWORD pid;
            HWND result;
            long long bestScore;
        } context{ processId, nullptr, -1 };

        EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL {
            auto* search = reinterpret_cast<SearchContext*>(parameter);
            if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return TRUE;

            DWORD windowPid = 0;
            GetWindowThreadProcessId(hwnd, &windowPid);
            if (windowPid != search->pid) return TRUE;

            const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if ((style & WS_CHILD) != 0) return TRUE;

            RECT rect{};
            if (!GetWindowRect(hwnd, &rect)) return TRUE;
            const LONG width = rect.right - rect.left;
            const LONG height = rect.bottom - rect.top;
            if (width < 320 || height < 240) return TRUE;

            wchar_t className[256]{};
            wchar_t title[512]{};
            GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
            GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));

            long long score = static_cast<long long>(width) * height;
            if (!GetWindow(hwnd, GW_OWNER)) score += 1000000000LL;
            if ((exStyle & WS_EX_TOOLWINDOW) == 0) score += 250000000LL;
            if (!IsIconic(hwnd)) score += 50000000LL;
            if (ContainsInsensitive(className, L"UnrealWindow")) score += 3000000000LL;
            if (ContainsInsensitive(title, L"Bodycam")) score += 1500000000LL;

            if (score > search->bestScore) {
                search->bestScore = score;
                search->result = hwnd;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&context));

        return context.result;
    }

    HWND WaitForWindowForProcess(DWORD processId, DWORD timeoutMs) {
        const ULONGLONG start = GetTickCount64();
        do {
            if (HWND window = FindWindowForProcess(processId)) return window;
            Sleep(250);
        } while (GetTickCount64() - start < timeoutMs);
        return nullptr;
    }

    bool IsSupportedBuild() {
        const uintptr_t base = GetRemoteModuleBase();
        if (!base) return false;
        auto plausible = [](uintptr_t p) { return p > 0x10000 && p < 0x7FFFFFFFFFFFull && (p & 7) == 0; };
        uintptr_t chunks = 0;
        int32_t maxEl = 0, numEl = 0, maxCh = 0, numCh = 0;
        const uintptr_t arr = base + OFF(GObjects);
        if (!ReadRemote(arr, chunks) || !ReadRemote(arr + 0x10, maxEl) || !ReadRemote(arr + 0x14, numEl) ||
            !ReadRemote(arr + 0x18, maxCh) || !ReadRemote(arr + 0x1C, numCh)) return false;
        if (!plausible(chunks) || numEl < 1000 || numEl > maxEl || maxEl > 0x4000000 ||
            numCh < 1 || numCh > maxCh || numCh != (numEl + 0xFFFF) / 0x10000) return false;
        uintptr_t chunk0 = 0, obj0 = 0;
        if (!ReadRemote(chunks, chunk0) || !plausible(chunk0) || !ReadRemote(chunk0, obj0) || !plausible(obj0)) return false;
        const uintptr_t cls = ReadPtr(obj0, OFF(ObjectClass));
        const uintptr_t meta = cls ? ReadPtr(cls, OFF(ObjectClass)) : 0;
        if (!plausible(cls) || !plausible(meta) || ReadPtr(meta, OFF(ObjectClass)) != meta) return false;
        const uintptr_t world = GetWorld();
        if (!plausible(world)) return false;
        const uintptr_t worldCls = ReadPtr(world, OFF(ObjectClass));
        uint32_t nameIdx = 0;
        if (!plausible(worldCls) || !ReadRemote(worldCls + OFF(ObjectName), nameIdx)) return false;
        return Skeleton_FName(nameIdx) == "world";
    }

    void RenderFrame() {
        const ImGuiIO& io = ImGui::GetIO();
        const float width = io.DisplaySize.x;
        const float height = io.DisplaySize.y;
        if (width < 1.0f || height < 1.0f) return;

        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        ESP_Render(g_ESP, drawList, width, height);
        Fx_Render(drawList, width, height);
        Aimbot_Render(g_Aim, width, height);
        Menu_RenderHud();
        Menu_Render();
        kit::apply_theme_mode();
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Log_InstallCrashHandler();
    Log_SetCrashHook(Host_Restore);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    timeBeginPeriod(1);
    Log_Add(Log_Info, "Utility started");
    Log_Add(Log_Info, "Offsets: %d entries, %s", static_cast<int>(g_offsets.count),
            g_offsets.sdkStamp[0] ? g_offsets.sdkStamp : "as built from source");

    bool attached = false;
    for (int attempt = 0; attempt < 5 && !attached; ++attempt) {
        attached = AttachToGameProcess();
        if (!attached) Sleep(200);
    }
    if (!attached) {
        Log_Add(Log_Info, "Bodycam is not running; exiting");
        return 0;
    }
    {
        bool supported = false;
        const ULONGLONG until = GetTickCount64() + 15000;
        do {
            supported = IsSupportedBuild();
            if (!supported) Sleep(300);
        } while (!supported && GetTickCount64() < until && IsGameProcessAttached());
        if (!supported) {
            Log_Add(Log_Error, "Unsupported Bodycam build (SDK offsets do not match this game); exiting");
            DetachFromGameProcess();
            return 0;
        }
    }

    const DWORD attachedPid = GetAttachedProcessId();
    Log_Add(Log_Info, "Attached to game (PID %lu)", static_cast<unsigned long>(attachedPid));
    GameSettings_Apply();
    HWND gameWindow = WaitForWindowForProcess(attachedPid, 180000);
    if (!gameWindow) {
        wchar_t message[512]{};
        swprintf_s(message, L"Bodycam process (PID %lu) was detected, but no visible game window was found.\n\nWait until the game is fully loaded, use windowed or borderless mode, then restart the utility.",
            static_cast<unsigned long>(attachedPid));
        MessageBoxW(nullptr, message, L"/var", MB_OK | MB_ICONERROR);
        DetachFromGameProcess();
        return 1;
    }

    if (!D3D12Overlay_Initialize(instance, RenderFrame)) {
        MessageBoxW(nullptr, D3D12Overlay_GetLastError(),
            L"/var - Overlay Initialization", MB_OK | MB_ICONERROR);
        DetachFromGameProcess();
        return 1;
    }
    bool loadToastPending = true;
    const ULONGLONG loadStart = GetTickCount64();
    ULONGLONG readySince = 0;

    while (D3D12Overlay_PumpMessages() && IsGameProcessAlive()) {
        if (loadToastPending) {
            const ULONGLONG t = GetTickCount64();
            const bool ready = GetWorld() && GetLocalPlayerController() && GetCameraManager() && Targets_CameraUsable(GetCameraCache());
            readySince = ready ? (readySince ? readySince : t) : 0;
            if ((readySince && t - readySince > 2000) || t - loadStart > 240000) {
                Hud_NotifyAlways(ICON_FA_CIRCLE_CHECK, "/var loaded  \xc2\xb7  press Insert to open", 5.0f);
                loadToastPending = false;
            }
        }
        if (!IsWindow(gameWindow)) {
            gameWindow = WaitForWindowForProcess(GetAttachedProcessId(), 10000);
            if (!gameWindow) break;
        }

        const bool paced = D3D12Overlay_WaitForFrame();
        MenuInput_SetActive(false, gameWindow);
        Host_FreezeInput(g_MenuOpen);
        D3D12Overlay_Update(gameWindow, g_MenuOpen);
        Visibility_Update();
        Targets_Update();
        Lens_Update();
        World_Update();
        Reload_Update();
        CombatLog_Update();
        Fx_Update();

        HWND foreground = GetForegroundWindow();
        DWORD foregroundPid = 0;
        if (foreground) GetWindowThreadProcessId(foreground, &foregroundPid);
        Misc_Update(foregroundPid == GetAttachedProcessId() && !g_MenuOpen);
        Host_Update();
        Ammo_Update(foregroundPid == GetAttachedProcessId() && !g_MenuOpen);
        if (foregroundPid == GetAttachedProcessId() && !g_MenuOpen)
            Aimbot_Tick(g_Aim);
        else
            Aimbot_Suspend();

        D3D12Overlay_Render();
        if (!paced) Sleep(1);
    }

    Config_SaveSession(true);
    g_Aim.Enabled = false;
    Log_Add(Log_Info, "Utility closing");
    D3D12Overlay_Shutdown();
    timeEndPeriod(1);
    Misc_Restore();
    Host_Restore();
    DetachFromGameProcess();
    return 0;
}
