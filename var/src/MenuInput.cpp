#include "../include/MenuInput.hpp"
#include <imgui.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace {
    struct Ev { enum { Key, Char, Wheel, HWheel } type; int a; bool down; };

    std::atomic<bool>  g_run{ false }, g_active{ false };
    std::atomic<DWORD> g_threadId{ 0 };
    bool               g_started = false;
    std::atomic<HWND>  g_game{ nullptr };
    std::atomic<LONG>  g_vx{ 0 }, g_vy{ 0 };
    std::atomic<uint8_t> g_down[256];
    std::atomic<bool>  g_caps{ false };
    std::mutex         g_mx;
    std::vector<Ev>    g_queue;

    constexpr ULONG_PTR kCancelTag = 0x56415243;

    LRESULT CALLBACK CancelWnd(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_INPUT && g_active) {
            RAWINPUT ri{};
            UINT sz = sizeof(ri);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
                ri.header.dwType == RIM_TYPEMOUSE && ri.header.hDevice &&
                !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                const LONG dx = ri.data.mouse.lLastX, dy = ri.data.mouse.lLastY;
                if (dx || dy) {
                    INPUT in{};
                    in.type = INPUT_MOUSE;
                    in.mi.dx = -dx; in.mi.dy = -dy;
                    in.mi.dwFlags = MOUSEEVENTF_MOVE;
                    in.mi.dwExtraInfo = kCancelTag;
                    SendInput(1, &in, sizeof(in));
                }
            }
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

    void CancelThread() {

        WNDCLASSW wc{};
        wc.lpfnWndProc = CancelWnd;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"VarRawCancel";
        RegisterClassW(&wc);
        HWND h = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (h) {
            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01; rid.usUsage = 0x02;
            rid.dwFlags = RIDEV_INPUTSINK;
            rid.hwndTarget = h;
            RegisterRawInputDevices(&rid, 1, sizeof(rid));
        }
        MSG msg;
        while (g_run && GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);

    }

    void Push(const Ev& e) { std::lock_guard<std::mutex> l(g_mx); if (g_queue.size() < 512) g_queue.push_back(e); }
    bool Down(int vk) { return vk > 0 && vk < 256 && g_down[vk].load() != 0; }

    void ClampToGame(LONG& x, LONG& y) {
        RECT r{};
        HWND g = g_game;
        if (!g || !GetClientRect(g, &r)) return;
        POINT a{ r.left, r.top }, b{ r.right, r.bottom };
        ClientToScreen(g, &a); ClientToScreen(g, &b);
        if (x < a.x) x = a.x; if (x > b.x - 1) x = b.x - 1;
        if (y < a.y) y = a.y; if (y > b.y - 1) y = b.y - 1;
    }

    LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp) {
        if (code != HC_ACTION || !g_active) return CallNextHookEx(nullptr, code, wp, lp);
        const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        if (m->dwExtraInfo == kCancelTag) return 1;
        if (m->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)) return CallNextHookEx(nullptr, code, wp, lp);
        switch (wp) {
        case WM_MOUSEMOVE: {
            POINT real{};
            GetCursorPos(&real);
            LONG x = g_vx + (m->pt.x - real.x), y = g_vy + (m->pt.y - real.y);
            ClampToGame(x, y);
            g_vx = x; g_vy = y;
            break;
        }
        case WM_LBUTTONDOWN: g_down[VK_LBUTTON] = 1; break;
        case WM_LBUTTONUP:   g_down[VK_LBUTTON] = 0; return CallNextHookEx(nullptr, code, wp, lp);
        case WM_RBUTTONDOWN: g_down[VK_RBUTTON] = 1; break;
        case WM_RBUTTONUP:   g_down[VK_RBUTTON] = 0; return CallNextHookEx(nullptr, code, wp, lp);
        case WM_MBUTTONDOWN: g_down[VK_MBUTTON] = 1; break;
        case WM_MBUTTONUP:   g_down[VK_MBUTTON] = 0; return CallNextHookEx(nullptr, code, wp, lp);
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
            g_down[HIWORD(m->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2] = wp == WM_XBUTTONDOWN;
            if (wp == WM_XBUTTONUP) return CallNextHookEx(nullptr, code, wp, lp);
            break;
        case WM_MOUSEWHEEL:  Push({ Ev::Wheel, static_cast<short>(HIWORD(m->mouseData)), false }); break;
        case WM_MOUSEHWHEEL: Push({ Ev::HWheel, static_cast<short>(HIWORD(m->mouseData)), false }); break;
        default: return CallNextHookEx(nullptr, code, wp, lp);
        }
        return 1;
    }

    LRESULT CALLBACK KeyHook(int code, WPARAM wp, LPARAM lp) {
        if (code != HC_ACTION || !g_active) return CallNextHookEx(nullptr, code, wp, lp);
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if (k->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) return CallNextHookEx(nullptr, code, wp, lp);
        const int vk = static_cast<int>(k->vkCode & 0xFF);
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        const bool altHeld = Down(VK_LMENU) || Down(VK_RMENU);
        const bool pass = vk == VK_INSERT || vk == VK_LWIN || vk == VK_RWIN || vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU || (altHeld && vk != VK_RMENU && !Down(VK_LCONTROL) && !Down(VK_RCONTROL));
        g_down[vk] = down;
        if (vk == VK_LSHIFT || vk == VK_RSHIFT) g_down[VK_SHIFT] = Down(VK_LSHIFT) || Down(VK_RSHIFT);
        if (vk == VK_LCONTROL || vk == VK_RCONTROL) g_down[VK_CONTROL] = Down(VK_LCONTROL) || Down(VK_RCONTROL);
        if (vk == VK_LMENU || vk == VK_RMENU) g_down[VK_MENU] = Down(VK_LMENU) || Down(VK_RMENU);
        if (vk == VK_CAPITAL && down) g_caps = !g_caps;
        if (pass) return CallNextHookEx(nullptr, code, wp, lp);
        Push({ Ev::Key, vk, down });
        if (!down) return CallNextHookEx(nullptr, code, wp, lp);
        if (down) {
            BYTE state[256] = {};
            for (int i = 0; i < 256; ++i) state[i] = Down(i) ? 0x80 : 0;
            if (g_caps) state[VK_CAPITAL] = 0x01;
            wchar_t buf[4] = {};
            const int n = ToUnicodeEx(k->vkCode, k->scanCode, state, buf, 4, 0x4  , GetKeyboardLayout(0));
            for (int i = 0; i < n; ++i) if (buf[i] >= 0x20 && buf[i] != 0x7F) Push({ Ev::Char, buf[i], true });
        }
        return 1;
    }

    void Thread() {
        g_threadId = GetCurrentThreadId();
        HHOOK mh = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, GetModuleHandleW(nullptr), 0);
        HHOOK kh = SetWindowsHookExW(WH_KEYBOARD_LL, KeyHook, GetModuleHandleW(nullptr), 0);
        MSG msg;
        while (g_run && GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (mh) UnhookWindowsHookEx(mh);
        if (kh) UnhookWindowsHookEx(kh);
        g_threadId = 0;
    }

    ImGuiKey ToImGuiKey(int vk) {
        if (vk >= '0' && vk <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + (vk - '0'));
        if (vk >= 'A' && vk <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + (vk - 'A'));
        if (vk >= VK_F1 && vk <= VK_F12) return static_cast<ImGuiKey>(ImGuiKey_F1 + (vk - VK_F1));
        switch (vk) {
        case VK_TAB: return ImGuiKey_Tab;           case VK_LEFT: return ImGuiKey_LeftArrow;
        case VK_RIGHT: return ImGuiKey_RightArrow;  case VK_UP: return ImGuiKey_UpArrow;
        case VK_DOWN: return ImGuiKey_DownArrow;    case VK_PRIOR: return ImGuiKey_PageUp;
        case VK_NEXT: return ImGuiKey_PageDown;     case VK_HOME: return ImGuiKey_Home;
        case VK_END: return ImGuiKey_End;           case VK_DELETE: return ImGuiKey_Delete;
        case VK_BACK: return ImGuiKey_Backspace;    case VK_SPACE: return ImGuiKey_Space;
        case VK_RETURN: return ImGuiKey_Enter;      case VK_ESCAPE: return ImGuiKey_Escape;
        case VK_LSHIFT: return ImGuiKey_LeftShift;  case VK_RSHIFT: return ImGuiKey_RightShift;
        case VK_LCONTROL: return ImGuiKey_LeftCtrl; case VK_RCONTROL: return ImGuiKey_RightCtrl;
        default: return ImGuiKey_None;
        }
    }
}

void MenuInput_SetActive(bool on, HWND gameWindow) {
    if (gameWindow) g_game = gameWindow;
    if (on == g_active.load()) return;
    if (on) {
        for (auto& d : g_down) d = 0;
        g_caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
        POINT p{}; GetCursorPos(&p);
        LONG x = p.x, y = p.y;
        RECT r{};
        if (gameWindow && GetClientRect(gameWindow, &r)) {
            POINT c{ (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
            ClientToScreen(gameWindow, &c);
            x = c.x; y = c.y;
        }
        g_vx = x; g_vy = y;
        { std::lock_guard<std::mutex> l(g_mx); g_queue.clear(); }
        if (!g_started) {
            g_started = true;
            g_run = true;
            std::thread(Thread).detach();
            std::thread(CancelThread).detach();
            for (int i = 0; i < 100 && !g_threadId; ++i) Sleep(1);
        }
        g_active = true;
    } else {
        g_active = false;
    }
}

bool MenuInput_Active() { return g_active; }

bool MenuInput_KeyDown(int vk) {
    if (g_active) return Down(vk);
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool MenuInput_CursorPos(POINT& p) {
    if (g_active) { p.x = g_vx; p.y = g_vy; return true; }
    return GetCursorPos(&p) != FALSE;
}

void MenuInput_Feed(ImGuiIO& io) {
    if (!g_active) return;
    std::vector<Ev> q;
    { std::lock_guard<std::mutex> l(g_mx); q.swap(g_queue); }
    for (const Ev& e : q) {
        switch (e.type) {
        case Ev::Key: { const ImGuiKey k = ToImGuiKey(e.a); if (k != ImGuiKey_None) io.AddKeyEvent(k, e.down); break; }
        case Ev::Char: io.AddInputCharacterUTF16(static_cast<ImWchar16>(e.a)); break;
        case Ev::Wheel: io.AddMouseWheelEvent(0.0f, static_cast<float>(e.a) / WHEEL_DELTA); break;
        case Ev::HWheel: io.AddMouseWheelEvent(-static_cast<float>(e.a) / WHEEL_DELTA, 0.0f); break;
        }
    }
    io.AddKeyEvent(ImGuiMod_Ctrl, Down(VK_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, Down(VK_SHIFT));
    io.AddKeyEvent(ImGuiMod_Alt, Down(VK_MENU));
    io.AddKeyEvent(ImGuiKey_LeftShift, Down(VK_LSHIFT));
    io.AddKeyEvent(ImGuiKey_RightShift, Down(VK_RSHIFT));
}
