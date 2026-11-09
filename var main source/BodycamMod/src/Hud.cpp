#include <cstring>
#include "../include/MediaSession.hpp"
#include "../include/Hud.hpp"
#include "../include/UiKit.hpp"
#include "../include/Aimbot.hpp"
#include "../include/Ammo.hpp"
#include "../include/Memory.hpp"
#include "../include/Targets.hpp"
#include "../include/World.hpp"
#include "../include/WeaponIcons.hpp"
#include "../include/CombatLog.hpp"
#include "../include/Reload.hpp"
#include "../include/Misc.hpp"
#include <Windows.h>
#include <cwctype>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <string>
#include <deque>
#include <string>

HudConfig g_Hud{};
extern AimbotConfig g_Aim;
const std::string& Menu_SteamPersona();

using namespace ImGui;
using kit::C;
using kit::Accent;

namespace {
    struct Toast { std::string icon, text; double start; double life; bool always; };
    std::deque<Toast> g_toasts;

    int   g_animLast[Hud_Count] = {};
    float g_animT[Hud_Count] = {};
    int   g_curWidget = -1;

    bool BeginWidget(int idx, ImVec2 size, ImVec2 defPos, bool menuOpen, ImVec2& pos) {
        {
            const int frame = GetFrameCount();
            if (g_animLast[idx] != frame - 1 && g_animLast[idx] != frame) g_animT[idx] = 0.0f;
            if (g_animLast[idx] != frame)
                g_animT[idx] = ImMin(1.0f, g_animT[idx] + ImMin(GetIO().DeltaTime, 1.0f / 30.0f) / 0.26f);
            g_animLast[idx] = frame;
            g_curWidget = idx;
        }
        const ImVec2 ds = GetIO().DisplaySize;
        const bool custom = g_Hud.PosX[idx] >= 0 && g_Hud.PosY[idx] >= 0;
        float px = custom ? g_Hud.PosX[idx] : defPos.x;
        float py = custom ? g_Hud.PosY[idx] : defPos.y;
        px = ImClamp(px, 0.0f, ImMax(0.0f, ds.x - size.x));
        py = ImClamp(py, 0.0f, ImMax(0.0f, ds.y - size.y));

        SetNextWindowPos({ px, py }, ImGuiCond_Always);
        SetNextWindowSize(size, ImGuiCond_Always);
        PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
        PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        char name[24];
        std::snprintf(name, sizeof(name), "##hud%d", idx);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoMove;
        if (!menuOpen) flags |= ImGuiWindowFlags_NoInputs;
        Begin(name, nullptr, flags);
        PopStyleVar(2);
        GetWindowDrawList()->PushClipRectFullScreen();

        if (menuOpen) {
            SetCursorPos({ 0, 0 });
            InvisibleButton("##drag", size);
            if (IsItemActive() && IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                px += GetIO().MouseDelta.x;
                py += GetIO().MouseDelta.y;
                g_Hud.PosX[idx] = ImMax(0.0f, px);
                g_Hud.PosY[idx] = ImMax(0.0f, py);
            }
            if (IsItemHovered() || IsItemActive()) SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        pos = { px, py };
        return true;
    }

    void EndWidget() {
        if (g_curWidget >= 0 && g_animT[g_curWidget] < 1.0f) {
            const float t = g_animT[g_curWidget], inv = t - 1.0f;
            const float back = 1.0f + 2.2f * inv * inv * inv + 1.2f * inv * inv;
            const float a = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
            const float sc = 0.94f + 0.06f * back, dr = (1.0f - a) * 10.0f;
            ImGuiWindow* w = GetCurrentWindow();
            const ImVec2 c = w->Pos + w->Size * 0.5f;
            for (ImDrawVert& v : w->DrawList->VtxBuffer) {
                v.pos.x = c.x + (v.pos.x - c.x) * sc;
                v.pos.y = c.y + (v.pos.y - c.y) * sc + dr;
                const unsigned al = (v.col >> IM_COL32_A_SHIFT) & 0xFF;
                v.col = (v.col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(al * a + 0.5f) << IM_COL32_A_SHIFT);
            }
        }
        g_curWidget = -1;
        GetWindowDrawList()->PopClipRect();
        End();
    }

    struct Seg { const char* icon; std::string text; bool brand; };

    float SegWidth(const Seg& s) {
        float w = 0;
        if (s.icon) w += kit::TextSize(kit::Regular(), 13.0f, s.icon).x + 8;
        if (s.brand) w += kit::TextSize(kit::Bold(), 14.0f, "/").x + 1;
        w += kit::TextSize(s.brand ? kit::Bold() : kit::Regular(), s.brand ? 14.0f : 12.5f, s.text.c_str()).x;
        return w;
    }

    float BarWidth(const std::vector<Seg>& segs) {
        float w = 28;
        for (size_t i = 0; i < segs.size(); ++i) w += SegWidth(segs[i]) + (i ? 25 : 0);
        return w;
    }

    void DrawBar(ImDrawList* dl, ImVec2 p, float w, float h, const std::vector<Seg>& segs) {
        kit::Card(dl, p, { p.x + w, p.y + h }, 9.0f);
        float x = p.x + 14;
        const float cy = p.y + h / 2;
        for (size_t i = 0; i < segs.size(); ++i) {
            const Seg& s = segs[i];
            if (i) {
                dl->AddLine({ x + 12, cy - 8 }, { x + 12, cy + 8 }, C(255, 255, 255, 26), 1.0f);
                x += 25;
            }
            if (s.icon) {
                kit::Text(dl, kit::Regular(), 13.0f, { x, cy - 7.5f }, s.brand ? Accent() : C(200, 200, 212), s.icon);
                x += kit::TextSize(kit::Regular(), 13.0f, s.icon).x + 8;
            }
            ImFont* f = s.brand ? kit::Bold() : kit::Regular();
            const float fs = s.brand ? 14.0f : 12.5f;
            if (s.brand) {
                kit::Text(dl, f, fs, { x, cy - fs * 0.6f }, Accent(), "/");
                x += kit::TextSize(f, fs, "/").x + 1;
            }
            kit::Text(dl, f, fs, { x, cy - fs * 0.6f }, C(236, 236, 242), s.text.c_str());
            x += kit::TextSize(f, fs, s.text.c_str()).x;
        }
    }

    void Watermark(bool menuOpen) {
        std::vector<Seg> segs;
        if (g_Hud.WmBrand) segs.push_back({ nullptr, "var", true });
        if (g_Hud.WmUser) {
            const std::string& n = Menu_SteamPersona();
            segs.push_back({ ICON_FA_USER, n.empty() ? "Offline" : n, false });
        }
        if (g_Hud.WmStatus) segs.push_back({ ICON_FA_SIGNAL, IsGameProcessAttached() ? "Attached" : "Waiting", false });
        if (g_Hud.WmFps) {
            char b[24]; std::snprintf(b, sizeof(b), "%.0f FPS", GetIO().Framerate);
            segs.push_back({ ICON_FA_GAUGE, b, false });
        }
        if (g_Hud.WmTime) {
            SYSTEMTIME t; GetLocalTime(&t);
            char b[16]; std::snprintf(b, sizeof(b), "%02d:%02d", t.wHour, t.wMinute);
            segs.push_back({ ICON_FA_CLOCK, b, false });
        }
        if (segs.empty()) return;
        const float w = BarWidth(segs), h = 36;
        ImVec2 p;
        BeginWidget(Hud_Watermark, { w, h }, { 16, 16 }, menuOpen, p);
        DrawBar(GetWindowDrawList(), p, w, h, segs);
        EndWidget();
    }

    struct AccentScope {
        ImVec4 saved;
        explicit AccentScope(int idx) : saved(kit::theme.accent) { if (g_Hud.CustomColor[idx]) kit::theme.accent = g_Hud.Color[idx]; }
        ~AccentScope() { kit::theme.accent = saved; }
    };

    ImU32 Lerp(ImU32 a, ImU32 b, float t) { return kit::mix(a, b, ImSaturate(t)); }

    constexpr float kCardW = 214.0f, kCardH = 58.0f, kBadge = 40.0f, kPad = 9.0f;
    constexpr float kTextX = kPad + kBadge + 11.0f;

    ImU32 WithA(ImU32 c, float a) {
        const float al = ((c >> IM_COL32_A_SHIFT) & 0xFF) * ImSaturate(a);
        return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(al + 0.5f) << IM_COL32_A_SHIFT);
    }

    void StatCard(ImDrawList* dl, ImVec2 p, float flash, ImU32 flashCol) {
        kit::Card(dl, p, { p.x + kCardW, p.y + kCardH }, 9.0f);
        if (flash > 0.01f) dl->AddRect(p, { p.x + kCardW, p.y + kCardH }, WithA(flashCol, flash), 9.0f, 0, 1.5f);
    }
    void Badge(ImDrawList* dl, ImVec2 p, ImU32 col, float glow = 0.0f) {
        const ImVec2 a{ p.x + kPad, p.y + kPad }, b{ a.x + kBadge, a.y + kBadge };
        dl->AddRectFilled(a, b, WithA(col, 0.13f + 0.14f * glow), 8.0f);
        dl->AddRectFilled(a, { b.x, a.y + kBadge * 0.5f }, C(255, 255, 255, static_cast<int>(6 + 10 * glow)), 8.0f, ImDrawFlags_RoundCornersTop);
        dl->AddRect(a, b, WithA(col, 0.30f + 0.40f * glow), 8.0f, 0, 1.0f);
    }
    void BadgeGlyph(ImDrawList* dl, ImVec2 p, const char* glyph, float size, ImU32 col, float dy = 0.0f) {
        const ImVec2 gs = kit::TextSize(kit::Regular(), size, glyph);
        kit::Text(dl, kit::Regular(), size, { ImFloor(p.x + kPad + (kBadge - gs.x) * 0.5f), ImFloor(p.y + kPad + (kBadge - gs.y) * 0.5f + dy) }, col, glyph);
    }
    void BadgeWeapon(ImDrawList* dl, ImVec2 p, int kind, const char* name, ImU32 col, float dy = 0.0f) {
        const float maxW = kBadge - 8, maxH = 16.0f;
        float h = maxH;
        const float w1 = WeaponIcon_Width(kind, 1.0f, name);
        if (w1 * h > maxW) h = maxW / w1;
        const float w = w1 * h;
        WeaponIcon_Draw(dl, kind, { ImFloor(p.x + kPad + (kBadge - w) * 0.5f), ImFloor(p.y + kPad + (kBadge - h) * 0.5f + dy) }, h, col, false, name);
    }
    void StatHeader(ImDrawList* dl, ImVec2 p, const char* label, ImU32 labelCol, const char* meta = nullptr, ImU32 metaCol = 0) {
        const float x = p.x + kTextX, r = p.x + kCardW - 12, y = p.y + 8;
        float metaX = r;
        if (meta && *meta) {
            metaX = r - kit::TextSize(kit::Regular(), 9.5f, meta).x;
            kit::Text(dl, kit::Regular(), 9.5f, { metaX, y }, metaCol, meta);
        }
        dl->PushClipRect({ x, p.y }, { metaX - 6, p.y + kCardH }, true);
        kit::Text(dl, kit::Bold(), 9.5f, { x, y }, labelCol, label);
        dl->PopClipRect();
    }
    void StatValue(ImDrawList* dl, ImVec2 p, const char* num, ImU32 numCol, const char* unit = nullptr) {
        const float x = p.x + kTextX, y = p.y + 18;
        kit::Text(dl, kit::Bold(), 22.0f, { x, y }, numCol, num);
        if (unit && *unit) {
            const float nw = kit::TextSize(kit::Bold(), 22.0f, num).x;
            kit::Text(dl, kit::Regular(), 11.5f, { x + nw + 5, y + 9 }, C(150, 150, 166), unit);
        }
    }
    ImVec2 BarA(ImVec2 p) { return { p.x + kTextX, p.y + kCardH - 11 }; }
    ImVec2 BarB(ImVec2 p) { return { p.x + kCardW - 12, p.y + kCardH - 8 }; }
    void StatBar(ImDrawList* dl, ImVec2 p, float frac, ImU32 col, float trail = -1.0f) {
        const ImVec2 a = BarA(p), b = BarB(p);
        const float w = b.x - a.x;
        dl->AddRectFilled(a, b, C(255, 255, 255, 18), 2.0f);
        if (trail > frac + 0.002f) dl->AddRectFilled({ a.x + w * ImSaturate(frac), a.y }, { a.x + w * ImSaturate(trail), b.y }, C(255, 255, 255, 140), 2.0f);
        if (frac > 0.002f) dl->AddRectFilled(a, { a.x + w * ImSaturate(frac), b.y }, col, 2.0f);
    }
    void StatPips(ImDrawList* dl, ImVec2 p, int segs, float fill, ImU32 col, float trail = -1.0f) {
        const ImVec2 a = BarA(p), b = BarB(p);
        segs = ImClamp(segs, 1, 40);
        const float gap = segs > 24 ? 1.0f : 2.0f;
        const float sw = ((b.x - a.x) - gap * (segs - 1)) / segs;
        for (int i = 0; i < segs; ++i) {
            const float x0 = a.x + i * (sw + gap), x1 = x0 + sw;
            dl->AddRectFilled({ x0, a.y }, { x1, b.y }, C(255, 255, 255, 18), 1.0f);
            const float tk = ImSaturate(trail - i), k = ImSaturate(fill - i);
            if (tk > k) dl->AddRectFilled({ x0 + sw * k, a.y }, { x0 + sw * tk, b.y }, C(255, 255, 255, 140), 1.0f);
            if (k > 0.0f) dl->AddRectFilled({ x0, a.y }, { x0 + sw * k, b.y }, col, 1.0f);
        }
    }
    float Flash(double at, double dur) {
        return ImMax(0.0f, 1.0f - static_cast<float>((GetTime() - at) / dur));
    }

    struct AmmoState {
        int kind = Weapon_None;
        std::string name;
        int cur = -1, mag = 0;
        bool have = false;
        double lastGood = -10.0;
        bool reloading = false;
        double reloadAt = -10.0, lastIncAt = -10.0, doneAt = -10.0, lastAnimAt = -10.0;
        int reloadFrom = 0;
        std::string reloadGun;
        std::unordered_map<std::string, float> len;
    };

    void AmmoWidget(bool menuOpen) {
        static AmmoState st;
        const uintptr_t localPawn = Targets_Get().localPawn;
        const HeldItem* held = localPawn ? World_HeldBy(localPawn) : nullptr;
        const double tnow = GetTime();
        const float dt = GetIO().DeltaTime;

        const int rawCur = Ammo_Current(), rawMag = Ammo_MaxSeen();
        const bool rawAmmo = Ammo_Learned() && rawCur >= 0;
        if (held) {
            const std::string nm = held->name;
            if (nm != st.name || held->kind != st.kind) {
                st.name = nm; st.kind = held->kind;
                st.reloading = false; st.lastIncAt = -10.0;
                st.cur = -1;
            }
        }
        const bool gun = st.kind != Weapon_Grenade && st.kind != Weapon_Knife && st.kind != Weapon_Other;
        if (rawAmmo && gun) {
            if (st.cur >= 2 && rawCur == 0 && !st.reloading) {
                st.reloading = true; st.reloadAt = tnow; st.reloadFrom = 0; st.reloadGun = st.name;
            }
            if (st.cur >= 0 && rawCur > st.cur) {
                st.lastIncAt = tnow;
                if (!st.reloading) { st.reloading = true; st.reloadAt = tnow - 0.25; st.reloadFrom = st.cur; st.reloadGun = st.name; }
            }
            st.cur = rawCur;
            st.mag = rawMag > 0 ? rawMag : st.mag;
            st.lastGood = tnow;
        }
        {
            static bool rWas = false;
            HWND fg = GetForegroundWindow(); DWORD fpid = 0;
            if (fg) GetWindowThreadProcessId(fg, &fpid);
            const bool rDown = !menuOpen && fpid == GetAttachedProcessId() && (GetAsyncKeyState('R') & 0x8000);
            if (rDown && !rWas && gun && st.cur >= 0 && st.mag > 0 && st.cur < st.mag && !st.reloading) {
                st.reloading = true; st.reloadAt = tnow; st.reloadFrom = st.cur; st.reloadGun = st.name;
            }
            rWas = rDown;
        }
        const bool animReload = Reload_Active();
        if (animReload && gun) st.lastAnimAt = tnow;
        if (((held && held->reloading) || animReload) && gun && !st.reloading) {
            st.reloading = true; st.reloadAt = tnow; st.reloadFrom = ImMax(0, st.cur); st.reloadGun = st.name;
        }
        if (st.reloading) {
            const bool full = st.mag > 0 && st.cur >= st.mag;
            const bool decoy = (held && held->reloading) || tnow - st.lastAnimAt < 0.15;
            const bool shells = st.lastIncAt > st.reloadAt;
            const bool settled = shells && tnow - st.lastIncAt > 0.9 && !decoy;
            const float expect = st.len.count(st.name) ? st.len[st.name] : 2.6f;
            const bool cancelled = !decoy && !shells && tnow - st.reloadAt > expect + 1.5;
            if ((full && !decoy) || settled || cancelled || tnow - st.reloadAt > 8.0 || st.name != st.reloadGun) {
                const float took = static_cast<float>(tnow - st.reloadAt);
                if (st.name == st.reloadGun && took > 0.4f && took < 8.0f) {
                    float& L = st.len[st.name];
                    L = L > 0.0f ? L * 0.5f + took * 0.5f : took;
                }
                st.reloading = false;
                st.doneAt = tnow;
            }
        }
        st.have = gun && st.cur >= 0 && (tnow - st.lastGood < 0.75 || st.reloading);

        const int kind = st.kind;
        const bool knife = kind == Weapon_Knife, grenade = kind == Weapon_Grenade;
        static double lastAny = -10.0;
        if (held || st.have) lastAny = tnow;
        if (!menuOpen && tnow - lastAny > 0.75) return;

        ImVec2 p;
        BeginWidget(Hud_Ammo, { kCardW, kCardH }, { 16, 60 }, menuOpen, p);
        ImDrawList* dl = GetWindowDrawList();
        const char* wname = !st.name.empty() ? st.name.c_str() : nullptr;

        static int lastCur = -1;
        static double shotAt = -10.0;
        if (st.have && lastCur >= 0 && st.cur < lastCur && !st.reloading) shotAt = tnow;
        if (st.have) lastCur = st.cur;
        const float shot = Flash(shotAt, 0.18);

        if (!st.have) {
            StatCard(dl, p, 0.0f, 0);
            const int ik = grenade ? Weapon_Grenade : knife ? Weapon_Knife : (kind != Weapon_None ? kind : Weapon_Rifle);
            Badge(dl, p, grenade ? Accent() : C(170, 170, 186));
            if (g_Hud.AmmoWeaponIcon) BadgeWeapon(dl, p, ik, wname, grenade ? Accent() : C(210, 210, 222));
            else BadgeGlyph(dl, p, grenade ? ICON_FA_BOMB : ICON_FA_GUN, 17.0f, grenade ? Accent() : C(210, 210, 222));
            StatHeader(dl, p, grenade ? "GRENADE" : knife ? "MELEE" : kind == Weapon_Other ? "GADGET" : "AMMO",
                       grenade ? Accent() : C(140, 140, 156), grenade ? "READY" : nullptr, Accent());
            const char* title = wname ? wname : grenade ? "Grenade" : knife ? "Knife" : "Weapon";
            dl->PushClipRect({ p.x + kTextX, p.y }, { p.x + kCardW - 10, p.y + kCardH }, true);
            kit::Text(dl, kit::Bold(), 15.0f, { p.x + kTextX, p.y + 23 }, C(240, 240, 246), title);
            dl->PopClipRect();
            EndWidget();
            return;
        }

        const int cur = st.cur, mag = st.mag;
        const bool reloading = st.reloading;
        const bool low = mag > 0 && cur * 100.0f <= mag * g_Hud.AmmoLowPct && !reloading;
        const float len = st.len.count(st.name) ? st.len[st.name] : 2.6f;
        float prog = 1.0f;
        if (reloading) {
            const float byTime = ImClamp(static_cast<float>(tnow - st.reloadAt) / len, 0.0f, 0.95f);
            const float byAmmo = mag > st.reloadFrom ? ImClamp((cur - st.reloadFrom) / static_cast<float>(mag - st.reloadFrom), 0.0f, 1.0f) : 0.0f;
            prog = st.lastIncAt > st.reloadAt ? ImMax(byAmmo, 0.05f) : byTime;
        }
        const float pe = 1.0f - std::pow(1.0f - prog, 2.2f);
        const float done = Flash(st.doneAt, 0.35);
        const ImU32 red = C(240, 80, 80);
        const ImU32 tone = reloading ? Lerp(C(120, 120, 132), Accent(), pe) : low ? red : Accent();

        StatCard(dl, p, ImMax(shot * 0.6f, done * 0.8f), low ? red : Accent());
        Badge(dl, p, tone, ImMax(shot, done));
        {
            const float dip = reloading ? std::sin(ImMin(1.0f, prog / 0.85f) * 3.14159265f) : 0.0f;
            const ImU32 ic = reloading ? Lerp(C(150, 150, 162), C(255, 255, 255), pe) : Lerp(low ? red : C(235, 235, 245), C(255, 255, 255), done);
            const int ik = kind != Weapon_None ? kind : Weapon_Rifle;
            if (g_Hud.AmmoWeaponIcon) BadgeWeapon(dl, p, ik, wname, ic, dip * 4.0f);
            else BadgeGlyph(dl, p, ICON_FA_GUN, 17.0f, ic, dip * 4.0f);
        }

        char num[16], tot[16], meta[24];
        int shownCur = cur;
        if (reloading && mag > 0) shownCur = ImMax(cur, st.reloadFrom + static_cast<int>((mag - st.reloadFrom) * pe));
        std::snprintf(num, sizeof(num), "%d", ImMin(shownCur, ImMax(mag, cur)));
        std::snprintf(tot, sizeof(tot), "/ %d", mag);
        if (reloading) std::snprintf(meta, sizeof(meta), "RELOADING");
        else if (low) std::snprintf(meta, sizeof(meta), "LOW AMMO");
        else meta[0] = 0;
        const ImU32 numCol = reloading ? Lerp(C(120, 120, 132), C(225, 225, 235), pe)
                           : low ? red : Lerp(Lerp(C(240, 240, 246), Accent(), shot), Accent(), done);
        StatHeader(dl, p, wname ? wname : "AMMO", C(140, 140, 156), meta, reloading ? Accent() : red);
        StatValue(dl, p, num, numCol, tot);

        if (g_Hud.AmmoBar && mag > 0) {
            static float shown = -1.0f;
            const float t = ImClamp(cur / static_cast<float>(mag), 0.0f, 1.0f);
            if (shown < 0.0f) shown = t;
            shown += (t - shown) * ImMin(1.0f, dt * 18.0f);
            if (mag <= 40) {
                const float fill = reloading ? pe * mag : shown * mag;
                StatPips(dl, p, mag, fill, reloading ? Accent(0.35f + 0.65f * pe) : tone);
            } else {
                StatBar(dl, p, reloading ? pe : shown, tone);
            }
        }
        EndWidget();
    }

    void KillsWidget(bool menuOpen) {
        const int k = World_LocalKills(), d = World_LocalDeaths();
        if (k < 0 && !menuOpen) return;
        const int kills = ImMax(0, k), deaths = ImMax(0, d);
        static int lastK = -1;
        static double killAt = -10.0;
        if (lastK >= 0 && kills > lastK) killAt = GetTime();
        lastK = kills;
        const float pop = Flash(killAt, 0.7);

        char kb[16], meta[32] = "";
        std::snprintf(kb, sizeof(kb), "%d", kills);
        const double kd = deaths ? kills / static_cast<double>(deaths) : static_cast<double>(kills);
        if (g_Hud.KillsKD) std::snprintf(meta, sizeof(meta), "%.2f K/D", kd);
        char unit[24] = "";
        if (g_Hud.KillsDeaths) std::snprintf(unit, sizeof(unit), "/ %d D", deaths);

        ImVec2 p;
        BeginWidget(Hud_Kills, { kCardW, kCardH }, { 16, 126 }, menuOpen, p);
        ImDrawList* dl = GetWindowDrawList();
        StatCard(dl, p, pop, Accent());
        Badge(dl, p, Accent(), pop);
        BadgeGlyph(dl, p, ICON_FA_SKULL, 18.0f + 3.0f * pop, Lerp(Accent(), C(255, 255, 255), 0.35f + 0.65f * pop));
        StatHeader(dl, p, "KILLS", C(140, 140, 156), meta, Lerp(C(170, 170, 186), Accent(), 0.6f));
        StatValue(dl, p, kb, Lerp(C(240, 240, 246), Accent(), 0.75f * pop), unit);
        {
            const ImVec2 a = BarA(p), b = BarB(p);
            const float tot = static_cast<float>(kills + deaths);
            const float kf = tot > 0.0f ? kills / tot : 0.0f;
            const float x = a.x + (b.x - a.x) * kf;
            dl->AddRectFilled(a, b, C(255, 255, 255, 18), 2.0f);
            if (deaths > 0) dl->AddRectFilled({ ImMin(b.x, x + (kills ? 2.0f : 0.0f)), a.y }, b, C(240, 80, 80, 150), 2.0f);
            if (kills > 0) dl->AddRectFilled(a, { x, b.y }, Accent(0.7f + 0.3f * pop), 2.0f);
        }
        EndWidget();
    }

    void HealthWidget(bool menuOpen) {
        const uintptr_t pawn = Targets_Get().localPawn;
        double hp = pawn ? GetPlayerHealth(pawn) : -1.0;
        double mx = pawn ? GetPlayerMaxHealth(pawn) : -1.0;
        const bool godVals = hp > 5000.0 || mx > 5000.0;
        if (godVals) { const float om = Misc_GodOrigMax(); mx = om > 1.0f && om < 5000.0f ? om : 100.0; hp = mx; }
        if (!(mx >= 1.0)) mx = 100.0;
        if (hp < 0.0) { if (!menuOpen) return; hp = 100.0; }
        const float frac = ImClamp(static_cast<float>(hp / mx), 0.0f, 1.0f);
        const float dt = GetIO().DeltaTime;

        static float shown = -1.0f, trail = -1.0f;
        static double hitAt = -10.0;
        if (shown < 0.0f) shown = trail = frac;
        if (frac < shown - 0.001f) hitAt = GetTime();
        shown += (frac - shown) * ImMin(1.0f, dt * 16.0f);
        if (trail < shown) trail = shown;
        else if (GetTime() - hitAt > 0.35) trail += (shown - trail) * ImMin(1.0f, dt * 5.0f);
        const float flash = Flash(hitAt, 0.4);

        const bool god = (g_Misc.GodMode && Misc_IsHost() == 1) || godVals;
        const ImU32 gold = C(255, 196, 64);
        const ImU32 hc = god ? gold : g_Hud.CustomColor[Hud_Health] ? Accent() : Lerp(C(240, 70, 80), C(80, 220, 140), frac);
        char nb[16], meta[24];
        std::snprintf(nb, sizeof(nb), "%.0f", hp);
        if (god) std::snprintf(meta, sizeof(meta), "INVINCIBLE");
        else std::snprintf(meta, sizeof(meta), "%.0f MAX", mx);
        ImVec2 p;
        BeginWidget(Hud_Health, { kCardW, kCardH }, { 16, 192 }, menuOpen, p);
        ImDrawList* dl = GetWindowDrawList();
        StatCard(dl, p, flash, C(240, 70, 80));
        const float beat = frac < 0.3f && !god ? 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 7.0f) : 0.0f;
        Badge(dl, p, hc, ImMax(flash, beat * 0.6f));
        BadgeGlyph(dl, p, god ? ICON_FA_SHIELD_HALVED : ICON_FA_HEART, 18.0f + 1.5f * beat, Lerp(hc, C(255, 255, 255), 0.25f + beat * 0.35f));
        StatHeader(dl, p, god ? "GOD MODE" : "HEALTH", god ? gold : C(140, 140, 156), meta, god ? gold : C(150, 150, 166));
        if (g_Hud.HealthNumber) StatValue(dl, p, nb, Lerp(C(240, 240, 246), C(255, 120, 120), flash * 0.8f), "HP");
        else StatValue(dl, p, "", C(240, 240, 246));
        StatPips(dl, p, 10, (god ? 1.0f : shown) * 10.0f, hc, g_Hud.HealthDamageTrail && !god ? trail * 10.0f : -1.0f);
        EndWidget();
    }

    struct FeedRow {
        const CombatEvent* e;
        double age;
        float w;
    };
    float FeedRowWidth(const CombatEvent& e) {
        const float iconH = 13.0f;
        float w = 10;
        if (!e.weapon.empty() || e.weaponKind) w += WeaponIcon_Width(e.weaponKind, iconH, e.weapon.c_str()) + 10;
        if (e.kind == Combat_Hit) {
            char d[16]; std::snprintf(d, sizeof(d), "-%.0f", e.dmg);
            w += ImMax(26.0f, kit::TextSize(kit::Bold(), 12.5f, d).x) + 8;
        } else w += 14 + 8;
        w += kit::TextSize(kit::Bold(), 12.0f, e.who.empty() ? "Enemy" : e.who.c_str()).x + 12;
        if (e.kind == Combat_Hit) w += 44 + 8 + kit::TextSize(kit::Regular(), 11.0f, "100 hp").x;
        else if (e.kind == Combat_Kill) {
            char m[128]; std::snprintf(m, sizeof(m), "%.0f m  \xc2\xb7  %d hit%s", e.distM, e.hits, e.hits == 1 ? "" : "s");
            w += kit::TextSize(kit::Regular(), 11.0f, m).x;
        } else w += kit::TextSize(kit::Regular(), 11.0f, e.reason.c_str()).x;
        return w + 12;
    }

    void FeedRowDraw(ImDrawList* dl, const CombatEvent& e, ImVec2 a, float w, float h) {
        const float alpha = GetStyle().Alpha;
        const ImVec4 cv = e.kind == Combat_Kill ? g_Hud.ClKillColor : e.kind == Combat_Hit ? g_Hud.ClHitColor : g_Hud.ClMissColor;
        auto colA = [&](float k) { return ColorConvertFloat4ToU32(ImVec4(cv.x, cv.y, cv.z, cv.w * k * alpha)); };
        const ImU32 white = C(240, 240, 246), muted = C(150, 150, 164);
        const ImVec2 b{ a.x + w, a.y + h };
        kit::Card(dl, a, b, 6.0f);
        if (e.kind == Combat_Kill) {
            dl->AddRectFilled(a, b, colA(0.08f), 6.0f);
            dl->AddRect(a, b, colA(0.55f), 6.0f, 0, 1.0f);
        }
        const float cy = a.y + h * 0.5f;
        float x = a.x + 10;
        if (!e.weapon.empty() || e.weaponKind) {
            const float ih = 13.0f, iw = WeaponIcon_Width(e.weaponKind, ih, e.weapon.c_str());
            WeaponIcon_Draw(dl, e.weaponKind, { ImFloor(x), ImFloor(cy - ih * 0.5f) }, ih, C(215, 215, 226), false, e.weapon.c_str());
            x += iw + 10;
        }
        if (e.kind == Combat_Hit) {
            char d[16]; std::snprintf(d, sizeof(d), "-%.0f", e.dmg);
            const ImVec2 ds = kit::TextSize(kit::Bold(), 12.5f, d);
            kit::Text(dl, kit::Bold(), 12.5f, { x, ImFloor(cy - ds.y * 0.5f) }, colA(1.0f), d);
            x += ImMax(26.0f, ds.x) + 8;
        } else {
            const char* g = e.kind == Combat_Kill ? ICON_FA_SKULL : ICON_FA_XMARK;
            const ImVec2 gs = kit::TextSize(kit::Regular(), 11.0f, g);
            kit::Text(dl, kit::Regular(), 11.0f, { ImFloor(x + (14 - gs.x) * 0.5f), ImFloor(cy - gs.y * 0.5f) }, colA(1.0f), g);
            x += 14 + 8;
        }
        const char* who = e.who.empty() ? "Enemy" : e.who.c_str();
        const ImVec2 ns = kit::TextSize(kit::Bold(), 12.0f, who);
        kit::Text(dl, kit::Bold(), 12.0f, { x, ImFloor(cy - ns.y * 0.5f) }, e.kind == Combat_Miss ? C(205, 205, 215) : white, who);
        x += ns.x + 12;
        if (e.kind == Combat_Hit) {
            const float frac = e.maxHp > 0.0f ? ImSaturate(e.hpLeft / e.maxHp) : 0.0f;
            const ImVec2 ba{ x, cy - 2 }, bb{ x + 44, cy + 2 };
            dl->AddRectFilled(ba, bb, C(255, 255, 255, 22), 2.0f);
            if (frac > 0.01f) dl->AddRectFilled(ba, { ba.x + 44 * frac, bb.y }, Lerp(C(240, 70, 80), C(80, 220, 140), frac), 2.0f);
            x += 44 + 8;
            char hp[16]; std::snprintf(hp, sizeof(hp), "%.0f hp", e.hpLeft);
            const ImVec2 hs = kit::TextSize(kit::Regular(), 11.0f, hp);
            kit::Text(dl, kit::Regular(), 11.0f, { x, ImFloor(cy - hs.y * 0.5f) }, muted, hp);
        } else {
            char m[128];
            if (e.kind == Combat_Kill) std::snprintf(m, sizeof(m), "%.0f m  \xc2\xb7  %d hit%s", e.distM, e.hits, e.hits == 1 ? "" : "s");
            else std::snprintf(m, sizeof(m), "%s", e.reason.c_str());
            const ImVec2 ms = kit::TextSize(kit::Regular(), 11.0f, m);
            kit::Text(dl, kit::Regular(), 11.0f, { x, ImFloor(cy - ms.y * 0.5f) }, muted, m);
        }
    }

    void CombatFeed(bool menuOpen) {
        const double now = Targets_Now();
        const double life = ImMax(1.0f, g_Hud.ClSeconds);
        std::vector<FeedRow> rows;
        static std::vector<CombatEvent> preview;
        const std::vector<CombatEvent>& ev = CombatLog_Events();
        for (auto it = ev.rbegin(); it != ev.rend() && static_cast<int>(rows.size()) < g_Hud.ClMaxLines; ++it) {
            const double age = now - it->time;
            if (age > life) break;
            if ((it->kind == Combat_Kill && !g_Hud.ClKills) || (it->kind == Combat_Hit && !g_Hud.ClHits) ||
                (it->kind == Combat_Miss && !g_Hud.ClMisses)) continue;
            rows.push_back({ &*it, age, 0.0f });
        }
        if (rows.empty() && menuOpen) {
            if (preview.empty()) {
                CombatEvent k; k.kind = Combat_Kill; k.who = "Bot 3"; k.weapon = "KA-74"; k.weaponKind = Weapon_Rifle; k.distM = 42; k.hits = 3;
                CombatEvent h; h.kind = Combat_Hit; h.who = "Bot 3"; h.weapon = "KA-74"; h.weaponKind = Weapon_Rifle; h.dmg = 34; h.hpLeft = 0; h.maxHp = 100;
                CombatEvent h2 = h; h2.dmg = 33; h2.hpLeft = 34;
                CombatEvent m; m.kind = Combat_Miss; m.who = "Bot 5"; m.weapon = "KA-74"; m.weaponKind = Weapon_Rifle; m.reason = "target moving";
                preview = { k, h, h2, m };
            }
            for (const CombatEvent& e : preview) {
                if ((e.kind == Combat_Miss && !g_Hud.ClMisses) || (e.kind == Combat_Hit && !g_Hud.ClHits) || (e.kind == Combat_Kill && !g_Hud.ClKills)) continue;
                rows.push_back({ &e, 1.0, 0.0f });
            }
        }
        if (rows.empty()) return;

        const float rowH = 26.0f, gap = 4.0f;
        float w = 220.0f;
        for (FeedRow& r : rows) { r.w = FeedRowWidth(*r.e); w = ImMax(w, r.w); }
        const float h = rows.size() * (rowH + gap) - gap;
        ImVec2 p;
        const ImVec2 ds = GetIO().DisplaySize;
        BeginWidget(Hud_CombatLog, { w, h }, { ds.x - w - 16.0f, 64.0f }, menuOpen, p);
        ImDrawList* dl = GetWindowDrawList();
        float y = p.y;
        for (const FeedRow& r : rows) {
            const float inT = ImMin(1.0f, static_cast<float>(r.age / 0.20));
            const float outT = static_cast<float>(ImClamp((life - r.age) / 0.45, 0.0, 1.0));
            const float ease = 1.0f - std::pow(1.0f - inT, 3.0f);
            const float a = ImMin(ease, outT);
            PushStyleVar(ImGuiStyleVar_Alpha, GetStyle().Alpha * a);
            const float x = p.x + w - r.w + (1.0f - ease) * 24.0f;
            FeedRowDraw(dl, *r.e, { x, y }, r.w, rowH);
            PopStyleVar();
            y += (rowH + gap) * ImMin(1.0f, outT * 2.0f);
        }
        EndWidget();
    }

    void Stats(bool menuOpen) {
        const TargetFrame& f = Targets_Get();
        int enemies = 0, visible = 0;
        for (const Target& t : f.targets) {
            if (t.friendly || t.drone) continue;
            ++enemies;
            if (t.visible) ++visible;
        }
        char a[32], b[32];
        std::snprintf(a, sizeof(a), "Enemies: %d", enemies);
        std::snprintf(b, sizeof(b), "Visible: %d", visible);
        std::vector<Seg> segs{ { ICON_FA_BOLT, a, false } };
        if (g_Hud.StatsVisible) segs.push_back({ ICON_FA_EYE, b, false });
        const float w = BarWidth(segs), h = 36;
        ImVec2 p;
        BeginWidget(Hud_Stats, { w, h }, { GetIO().DisplaySize.x - w - 16, 16 }, menuOpen, p);
        DrawBar(GetWindowDrawList(), p, w, h, segs);
        EndWidget();
    }

    void Keybinds(bool menuOpen) {
        struct Bind { const char* label; const char* mode; std::string key; bool active; };
        std::vector<Bind> binds;
        static const char* const kModes[] = { "Hold", "Toggle", "Always" };
        const bool aimKey = g_Aim.AimKey && (GetAsyncKeyState(g_Aim.AimKey) & 0x8000);
        const bool aimActive = g_Aim.Enabled && (g_Aim.KeyMode == AimKeyMode_Always ||
                               (g_Aim.KeyMode == AimKeyMode_Hold && aimKey) ||
                               (g_Aim.KeyMode == AimKeyMode_Toggle && Aimbot_Toggled()));
        binds.push_back({ "Aimbot", kModes[ImClamp(g_Aim.KeyMode, 0, 2)],
                          g_Aim.KeyMode == AimKeyMode_Always ? "-" : kit::KeyName(g_Aim.AimKey), aimActive });
        const bool trigKey = g_Aim.TriggerKey && (GetAsyncKeyState(g_Aim.TriggerKey) & 0x8000);
        binds.push_back({ "Triggerbot", g_Aim.TriggerKey ? "Hold" : "Always",
                          g_Aim.TriggerKey ? kit::KeyName(g_Aim.TriggerKey) : "-",
                          g_Aim.Triggerbot && (g_Aim.TriggerKey == 0 || trigKey) });
        binds.push_back({ "Menu", "Toggle", "Insert", menuOpen });
        if (g_Hud.KbOnlyActive && !menuOpen)
            binds.erase(std::remove_if(binds.begin(), binds.end(), [](const Bind& b) { return !b.active; }), binds.end());

        const float w = 236, rowH = 22, h = 40 + rowH * binds.size() + 6;
        ImVec2 p;
        BeginWidget(Hud_Keybinds, { w, h }, { 16, GetIO().DisplaySize.y * 0.30f }, menuOpen, p);
        ImDrawList* dl = GetWindowDrawList();
        kit::Card(dl, p, { p.x + w, p.y + h }, 9.0f);
        kit::Text(dl, kit::Regular(), 13.0f, { p.x + 14, p.y + 12 }, C(200, 200, 212), ICON_FA_KEYBOARD);
        kit::Text(dl, kit::Regular(), 12.5f, { p.x + 38, p.y + 12 }, C(236, 236, 242), "Keybinds");
        kit::Text(dl, kit::Regular(), 11.0f, { p.x + w - 24, p.y + 13 }, C(120, 120, 134), ICON_FA_THUMBTACK);

        float y = p.y + 40;
        for (const Bind& b : binds) {
            const ImU32 lc = b.active ? C(240, 240, 246) : C(140, 140, 152);
            kit::Text(dl, kit::Regular(), 12.0f, { p.x + 14, y + 3 }, lc, b.label);
            const ImVec2 ks = kit::TextSize(kit::Bold(), 12.0f, b.key.c_str());
            const float keyX = p.x + w - 14 - ks.x;
            kit::Text(dl, kit::Bold(), 12.0f, { keyX, y + 3 }, b.active ? Accent() : C(230, 230, 238), b.key.c_str());
            const ImVec2 ms = kit::TextSize(kit::Regular(), 11.5f, b.mode);
            kit::Text(dl, kit::Regular(), 11.5f, { keyX - 14 - ms.x, y + 3.5f }, C(120, 120, 134), b.mode);
            y += rowH;
        }
        EndWidget();
    }

    void TargetHud(bool menuOpen) {
        static Target last;
        static bool have = false;
        static double lastSeen = -10.0;
        static float alpha = 0.0f;

        const uintptr_t locked = Aimbot_LockedTarget();
        bool live = false;
        if (locked) {
            for (const Target& t : Targets_Get().targets)
                if (t.pawn == locked) { last = t; have = true; live = true; lastSeen = GetTime(); break; }
        }
        Target t = last;
        bool preview = false;
        if (!live && menuOpen && (!have || GetTime() - lastSeen > 0.4)) {
            t = Target{}; t.name = "Target"; t.distM = 42.0f; t.health = 75.0; t.maxHealth = 100.0;
            preview = true;
        }
        const bool show = live || preview || (have && GetTime() - lastSeen < 0.4);
        alpha = ImLerp(alpha, show ? 1.0f : 0.0f, ImMin(1.0f, GetIO().DeltaTime * 12.0f));
        if (alpha < 0.02f) return;

        const bool circle = g_Hud.TargetStyle == TargetHud_Circle && g_Hud.TargetBar;
        const float w = circle ? 214.0f : 200.0f, h = 58.0f;
        ImVec2 p;
        const ImVec2 ds = GetIO().DisplaySize;
        PushStyleVar(ImGuiStyleVar_Alpha, GetStyle().Alpha * alpha);
        BeginWidget(Hud_Target, { w, h }, { ds.x - w - 16, ds.y * 0.42f }, menuOpen, p);
        ImDrawList* dl = GetWindowDrawList();
        kit::Card(dl, p, { p.x + w, p.y + h }, 9.0f);

        const ImVec2 a0(p.x + 9, p.y + 9), a1(p.x + 49, p.y + 49);
        dl->AddRectFilled(a0, a1, C(36, 36, 44), 7.0f);
        const char* icon = t.drone ? ICON_FA_LOCATION_CROSSHAIRS : t.bot ? ICON_FA_ROBOT : ICON_FA_USER;
        const ImVec2 is = kit::TextSize(kit::Regular(), 18.0f, icon);
        kit::Text(dl, kit::Regular(), 18.0f, ImVec2((a0.x + a1.x) / 2, (a0.y + a1.y) / 2) - is * 0.5f, C(235, 235, 242), icon);

        const double frac = (t.health >= 0 && t.maxHealth > 0) ? ImClamp(t.health / t.maxHealth, 0.0, 1.0) : -1.0;
        const float textX = p.x + 60;
        std::string name = g_Hud.TargetName ? (t.name.empty() ? (t.bot ? "Bot" : "Player") : t.name) : (t.bot ? "Bot" : "Player");
        const float nameMax = (circle ? w - 56 : w - 12) - (textX - p.x);
        dl->PushClipRect({ textX, p.y }, { textX + nameMax, p.y + h }, true);
        kit::Text(dl, kit::Bold(), 13.0f, { textX, p.y + 11 }, C(240, 240, 246), name.c_str());
        dl->PopClipRect();

        char sub[64];
        const char* kind = t.drone ? "Drone" : t.bot ? "Bot" : "Player";
        if (g_Hud.TargetDist) std::snprintf(sub, sizeof(sub), "%s  \xc2\xb7  %.0f m", kind, t.distM);
        else std::snprintf(sub, sizeof(sub), "%s", kind);
        kit::Text(dl, kit::Regular(), 11.0f, { textX, p.y + 28 }, C(130, 130, 144), sub);

        const ImU32 hpCol = frac < 0 ? C(120, 120, 134)
            : ColorConvertFloat4ToU32(ImLerp(ImVec4(0.95f, 0.30f, 0.35f, GetStyle().Alpha), ImVec4(0.30f, 0.86f, 0.55f, GetStyle().Alpha), static_cast<float>(frac)));
        if (g_Hud.TargetBar) {
            if (circle) {
                const ImVec2 c(p.x + w - 30, p.y + h / 2);
                const float r = 17.0f;
                dl->PathArcTo(c, r, 0, IM_PI * 2, 40);
                dl->PathStroke(C(50, 50, 60), 0, 4.0f);
                if (frac > 0) {
                    dl->PathArcTo(c, r, -IM_PI / 2, -IM_PI / 2 + IM_PI * 2 * static_cast<float>(frac), 40);
                    dl->PathStroke(hpCol, 0, 4.0f);
                }
                char pct[16];
                if (frac < 0) std::snprintf(pct, sizeof(pct), "?");
                else std::snprintf(pct, sizeof(pct), "%.0f%%", frac * 100.0);
                const ImVec2 ps = kit::TextSize(kit::Bold(), 10.5f, pct);
                kit::Text(dl, kit::Bold(), 10.5f, c - ps * 0.5f, C(236, 236, 242), pct);
            } else {
                const ImVec2 b0(textX, p.y + 45), b1(p.x + w - 12, p.y + 48);
                dl->AddRectFilled(b0, b1, C(50, 50, 60), 2.0f);
                if (frac > 0) dl->AddRectFilled(b0, { b0.x + (b1.x - b0.x) * static_cast<float>(frac), b1.y }, hpCol, 2.0f);
            }
        }
        EndWidget();
        PopStyleVar();
    }

    struct NowPlaying { std::string title, artist, source; bool playing = false; };

    std::string WideToUtf8(const wchar_t* w) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? n - 1 : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
        return s;
    }

    std::wstring ProcessName(DWORD pid) {
        std::wstring out;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return out;
        wchar_t buf[MAX_PATH]; DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
            out = buf;
            const size_t s = out.find_last_of(L"\\/");
            if (s != std::wstring::npos) out = out.substr(s + 1);
            for (wchar_t& c : out) c = static_cast<wchar_t>(towlower(c));
        }
        CloseHandle(h);
        return out;
    }

    NowPlaying QueryNowPlaying() {
        struct Ctx { NowPlaying spotify, browser; bool spotifySeen = false; } ctx;
        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            if (!IsWindowVisible(hwnd) && !IsIconic(hwnd)) return TRUE;
            wchar_t title[512] = {};
            if (GetWindowTextW(hwnd, title, 512) <= 0) return TRUE;
            DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
            const std::wstring exe = ProcessName(pid);
            const std::wstring t = title;
            if (exe == L"spotify.exe") {
                c->spotifySeen = true;
                const size_t d = t.find(L" - ");
                if (d != std::wstring::npos && t.rfind(L"Spotify", 0) != 0) {
                    c->spotify.artist = WideToUtf8(t.substr(0, d).c_str());
                    c->spotify.title = WideToUtf8(t.substr(d + 3).c_str());
                    c->spotify.playing = true;
                    c->spotify.source = "Spotify";
                }
            } else if (c->browser.title.empty() && (exe == L"chrome.exe" || exe == L"msedge.exe" || exe == L"firefox.exe" ||
                                                     exe == L"brave.exe" || exe == L"opera.exe" || exe == L"opera_gx.exe")) {
                const size_t y = t.find(L" - YouTube");
                if (y != std::wstring::npos && y > 0) {
                    std::wstring song = t.substr(0, y);
                    if (song.size() > 4 && song[0] == L'(' ) { const size_t e = song.find(L") "); if (e != std::wstring::npos && e < 6) song = song.substr(e + 2); }
                    c->browser.title = WideToUtf8(song.c_str());
                    c->browser.artist = "YouTube";
                    c->browser.source = "YouTube";
                    c->browser.playing = true;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.spotify.playing) return ctx.spotify;
        if (ctx.browser.playing) return ctx.browser;
        NowPlaying idle;
        idle.source = ctx.spotifySeen ? "Spotify" : "";
        idle.title = ctx.spotifySeen ? "Paused" : "Nothing playing";
        idle.artist = ctx.spotifySeen ? "Spotify" : "Play something on your PC";
        return idle;
    }

    const NowPlaying& Playing() {
        static NowPlaying np;
        static double next = 0.0;
        if (GetTime() >= next) { np = QueryNowPlaying(); next = GetTime() + 0.5; }
        return np;
    }

    void MediaKey(BYTE vk) {
        INPUT in[2] = {};
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = vk;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
    }

    ImTextureData* g_artTex = nullptr;
    uint32_t g_artVer = 0;
    bool g_hasArt = false;
    void UpdateArt(uint32_t ver) {
        if (ver == g_artVer && g_artTex) return;
        std::vector<uint8_t> rgba;
        g_artVer = Media_GetArt(rgba);
        g_hasArt = rgba.size() == 128 * 128 * 4;
        if (!g_hasArt) return;
        if (!g_artTex) {
            g_artTex = IM_NEW(ImTextureData)();
            g_artTex->Create(ImTextureFormat_RGBA32, 128, 128);
            RegisterUserTexture(g_artTex);
        }
        std::memcpy(g_artTex->GetPixels(), rgba.data(), rgba.size());
        if (g_artTex->Status == ImTextureStatus_OK) {
            ImTextureRect r{ 0, 0, 128, 128 };
            g_artTex->Updates.push_back(r);
            g_artTex->UpdateRect = r;
            g_artTex->UsedRect = r;
            g_artTex->SetStatus(ImTextureStatus_WantUpdates);
        } else if (g_artTex->Status == ImTextureStatus_Destroyed) {
            g_artTex->SetStatus(ImTextureStatus_WantCreate);
        }
    }

    void MarqueeText(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, float avail, ImU32 col, const char* text, double since) {
        const float tw = kit::TextSize(f, size, text).x;
        float off = 0.0f;
        if (tw > avail) {
            const float travel = tw - avail + 8.0f, speed = 26.0f, rest = 2.2f;
            const float cycle = rest + travel / speed + rest;
            const float t = std::fmod(static_cast<float>(GetTime() - since), cycle);
            off = t < rest ? 0.0f : t < rest + travel / speed ? (t - rest) * speed : travel;
        }
        dl->PushClipRect({ pos.x, pos.y - 4 }, { pos.x + avail, pos.y + size + 6 }, true);
        const int v0 = dl->VtxBuffer.Size;
        kit::Text(dl, f, size, { ImFloor(pos.x - off), pos.y }, col, text);
        if (tw > avail) {
            const float fade = 14.0f, l = pos.x, r = pos.x + avail;
            const bool leftFade = off > 0.5f;
            for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
                ImDrawVert& v = dl->VtxBuffer[i];
                float k = ImSaturate((r - v.pos.x) / fade);
                if (leftFade) k = ImMin(k, ImSaturate((v.pos.x - l) / fade));
                const unsigned al = (v.col >> IM_COL32_A_SHIFT) & 0xFF;
                v.col = (v.col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(al * k + 0.5f) << IM_COL32_A_SHIFT);
            }
        }
        dl->PopClipRect();
    }

    void MusicWidget(bool menuOpen) {
        MediaInfo mi;
        const bool sys = Media_Get(mi);
        NowPlaying np;
        bool idle;
        if (sys) {
            idle = !mi.available || mi.title.empty();
            np.title = idle ? "Nothing playing" : mi.title;
            np.artist = idle ? "Play something on your PC" : (mi.artist.empty() ? mi.album : mi.artist);
            np.source = idle ? "" : mi.source;
            np.playing = !idle && mi.playing;
            UpdateArt(mi.artVersion);
            if (idle) g_hasArt = false;
        } else {
            np = Playing();
            idle = np.title.empty() || np.title == "Nothing playing";
            g_hasArt = false;
        }
        const bool timed = sys && !idle && mi.duration > 0.5;

        static std::string lastTitle;
        static double titleSince = 0.0, shownPos = 0.0, lastT = 0.0;
        const double nowT = GetTime();
        if (np.title != lastTitle) { lastTitle = np.title; titleSince = nowT; shownPos = timed ? mi.position : 0.0; }
        if (timed) {
            const double pred = shownPos + (np.playing ? nowT - lastT : 0.0);
            shownPos = std::fabs(mi.position - pred) > 1.2 ? mi.position : pred + (mi.position - pred) * 0.05;
            shownPos = ImClamp(shownPos, 0.0, mi.duration);
        }
        lastT = nowT;

        const bool buttons = g_Hud.MusicButtons;
        const float w = 300.0f, h = 86.0f;
        ImVec2 p;
        BeginWidget(Hud_Music, { w, h }, { 16, GetIO().DisplaySize.y - h - 110.0f }, menuOpen, p);
        ImDrawList* dl = GetWindowDrawList();
        const ImVec2 q(p.x + w, p.y + h);
        kit::Card(dl, p, q, 9.0f);

        const ImVec2 aa(p.x + 12, p.y + 12), ab(p.x + 60, p.y + 60);
        const bool bars = np.playing && g_Hud.MusicBars;
        auto drawBars = [&](ImVec2 base, float maxH, float bw, float gap, ImU32 col) {
            const float t = static_cast<float>(nowT);
            for (int i = 0; i < 3; ++i) {
                const float k = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * (5.1f + i * 1.7f) + i * 2.1f));
                const float bx = base.x + i * (bw + gap);
                dl->AddRectFilled({ bx, base.y - maxH * k }, { bx + bw, base.y }, col, bw * 0.5f);
            }
        };
        if (g_hasArt && g_artTex) {
            dl->AddImageRounded(ImTextureRef(g_artTex->GetTexRef()), aa, ab, { 0, 0 }, { 1, 1 }, C(255, 255, 255), 7.0f);
            if (bars) {
                const ImVec2 ca(ab.x - 19, ab.y - 16), cb(ab.x - 3, ab.y - 3);
                dl->AddRectFilled(ca, cb, C(0, 0, 0, 150), 4.0f);
                drawBars({ ca.x + 3.5f, cb.y - 3 }, 7.0f, 2.0f, 1.5f, C(255, 255, 255, 235));
            }
        } else {
            dl->AddRectFilled(aa, ab, C(40, 40, 49), 7.0f);
            if (bars) drawBars({ aa.x + 15.5f, ab.y - 15 }, 18.0f, 4.0f, 3.0f, Accent());
            else kit::TextCentered(dl, kit::Regular(), 16.0f, { (aa.x + ab.x) * 0.5f, (aa.y + ab.y) * 0.5f }, C(150, 150, 164), ICON_FA_MUSIC);
        }

        const float ctrlW = buttons ? 96.0f : 0.0f;
        if (buttons) {
            struct B { const char* icon; BYTE vk; MediaCommand cmd; };
            const B bs[3] = { { ICON_FA_BACKWARD_STEP, VK_MEDIA_PREV_TRACK, Media_Previous },
                              { np.playing ? ICON_FA_PAUSE : ICON_FA_PLAY, VK_MEDIA_PLAY_PAUSE, Media_PlayPause },
                              { ICON_FA_FORWARD_STEP, VK_MEDIA_NEXT_TRACK, Media_Next } };
            const float cy = (aa.y + ab.y) * 0.5f, cx = q.x - 12 - ctrlW * 0.5f;
            for (int i = 0; i < 3; ++i) {
                const bool main = i == 1;
                const ImVec2 c(cx + (i - 1) * 32.0f, cy);
                const float rad = main ? 15.0f : 12.0f;
                const ImRect r({ c.x - rad, c.y - rad }, { c.x + rad, c.y + rad });
                const bool hov = menuOpen && r.Contains(GetIO().MousePos);
                const bool down = hov && IsMouseDown(ImGuiMouseButton_Left);
                const float ht = kit::anim(GetID(i == 0 ? "##mprev" : i == 1 ? "##mplay" : "##mnext"), hov ? 1.0f : 0.0f, 18.0f, 0.0f);
                const float sc = down ? 0.92f : 1.0f;
                if (main) {
                    dl->AddCircleFilled(c, rad * sc, kit::mix(C(236, 236, 242), C(255, 255, 255), ht), 40);
                    kit::TextCentered(dl, kit::Regular(), 11.0f * sc, { c.x + (np.playing ? 0.0f : 1.0f), c.y }, C(22, 22, 28), bs[i].icon);
                } else {
                    if (ht > 0.01f) dl->AddCircleFilled(c, rad, C(255, 255, 255, static_cast<int>(16 * ht)), 32);
                    kit::TextCentered(dl, kit::Regular(), 11.0f * sc, c, kit::mix(C(190, 190, 202), C(255, 255, 255), ht), bs[i].icon);
                }
                if (hov) SetMouseCursor(ImGuiMouseCursor_Hand);
                if (hov && IsMouseReleased(ImGuiMouseButton_Left) && GetIO().MouseDragMaxDistanceSqr[0] < 16.0f) {
                    if (sys && mi.available) Media_Send(bs[i].cmd);
                    else MediaKey(bs[i].vk);
                }
            }
        }

        const float tx = ab.x + 12, avail = q.x - 12 - ctrlW - (buttons ? 8.0f : 0.0f) - tx;
        MarqueeText(dl, kit::Bold(), 13.0f, { tx, p.y + 18 }, avail, idle ? C(200, 200, 212) : C(245, 245, 250), np.title.c_str(), titleSince);
        std::string sub = np.artist;
        if (!np.source.empty()) sub += (sub.empty() ? "" : "  \xc2\xb7  ") + np.source;
        MarqueeText(dl, kit::Regular(), 11.5f, { tx, p.y + 37 }, avail, C(140, 140, 156), sub.c_str(), titleSince);

        {
            const float by = q.y - 14;
            char eb[16] = "", rb[16] = "";
            auto fmt = [](double sec, char* b, size_t n, bool minus) {
                const int s = static_cast<int>(sec < 0 ? 0 : sec);
                std::snprintf(b, n, "%s%d:%02d", minus ? "-" : "", s / 60, s % 60);
            };
            if (timed) { fmt(shownPos, eb, sizeof(eb), false); fmt(mi.duration - shownPos, rb, sizeof(rb), true); }
            else if (!idle) fmt(sys ? mi.position : (np.playing ? nowT - titleSince : 0.0), eb, sizeof(eb), false);
            const float fs = 10.0f;
            const float lw = ImMax(kit::TextSize(kit::Regular(), fs, "00:00").x, kit::TextSize(kit::Regular(), fs, eb).x);
            const float rw = ImMax(kit::TextSize(kit::Regular(), fs, "-00:00").x, kit::TextSize(kit::Regular(), fs, rb).x);
            const float x0 = p.x + 12, x1 = q.x - 12;
            const float bx0 = x0 + lw + 8, bx1 = x1 - rw - 8;
            const float ty = kit::MidY(kit::Regular(), fs, by);
            kit::Text(dl, kit::Regular(), fs, { x0, ty }, C(130, 130, 144), eb);
            const ImVec2 rs = kit::TextSize(kit::Regular(), fs, rb);
            kit::Text(dl, kit::Regular(), fs, { x1 - rs.x, ty }, C(130, 130, 144), rb);
            dl->AddRectFilled({ bx0, by - 1.5f }, { bx1, by + 1.5f }, C(255, 255, 255, 24), 1.5f);
            const float f = timed ? static_cast<float>(ImClamp(shownPos / mi.duration, 0.0, 1.0)) : 0.0f;
            if (f > 0.002f) dl->AddRectFilled({ bx0, by - 1.5f }, { bx0 + (bx1 - bx0) * f, by + 1.5f }, Accent(), 1.5f);
        }
        EndWidget();
    }

    void Toasts() {
        const double now = GetTime();
        g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(), [&](const Toast& t) { return now - t.start > t.life; }), g_toasts.end());
        if (g_toasts.empty()) return;

        ImDrawList* dl = GetBackgroundDrawList();
        const ImVec2 ds = GetIO().DisplaySize;
        float y = ds.y - 20;
        for (auto it = g_toasts.rbegin(); it != g_toasts.rend(); ++it) {
            const double age = now - it->start;
            const double life = it->life;
            const float inT = static_cast<float>(ImMin(1.0, age / 0.22));
            const float outT = static_cast<float>(ImClamp((life - age) / 0.25, 0.0, 1.0));
            const float ease = 1.0f - std::pow(1.0f - inT, 3.0f);
            const float a = ImMin(ease, outT);

            const float fs = 12.5f;
            const float tw = kit::TextSize(kit::Regular(), fs, it->text.c_str()).x;
            const float w = tw + 72, h = 44;
            const float x = 16 - (1.0f - ease) * (w * 0.4f);
            y -= h;
            PushStyleVar(ImGuiStyleVar_Alpha, GetStyle().Alpha * a);
            kit::Card(dl, { x, y }, { x + w, y + h }, 9.0f);
            kit::Text(dl, kit::Regular(), 13.0f, { x + 15, y + 13 }, C(220, 220, 230), it->icon.c_str());
            dl->AddLine({ x + 42, y + 13 }, { x + 42, y + 29 }, C(255, 255, 255, 30));
            kit::Text(dl, kit::Regular(), fs, { x + 54, y + 13.5f }, C(236, 236, 242), it->text.c_str());
            const float prog = 1.0f - static_cast<float>(age / life);
            dl->AddRectFilled({ x + 12, y + h - 6 }, { x + 12 + (w - 24) * prog, y + h - 3.5f }, Accent(), 2.0f);
            PopStyleVar();
            y -= 8 * a + 0.0f;
        }
    }
}

void Hud_Notify(const char* icon, const char* text) {
    if (!g_Hud.Notifications) return;
    g_toasts.push_back({ icon ? icon : ICON_FA_BELL, text, GetTime(), ImMax(0.5, static_cast<double>(g_Hud.NotifySeconds)), false });
    while (g_toasts.size() > 5) g_toasts.pop_front();
}

void Hud_ResetPositions() {
    for (int i = 0; i < Hud_Count; ++i) { g_Hud.PosX[i] = -1; g_Hud.PosY[i] = -1; }
}

void Hud_Render(bool menuOpen) {
    PushStyleVar(ImGuiStyleVar_Alpha, kit::theme.hudAlpha);
    if (g_Hud.Watermark) { AccentScope s(Hud_Watermark); Watermark(menuOpen); }
    if (g_Hud.Ammo)      { AccentScope s(Hud_Ammo); AmmoWidget(menuOpen); }
    if (g_Hud.Kills)     { AccentScope s(Hud_Kills); KillsWidget(menuOpen); }
    if (g_Hud.Health)    { AccentScope s(Hud_Health); HealthWidget(menuOpen); }
    if (g_Hud.CombatLog) { AccentScope s(Hud_CombatLog); CombatFeed(menuOpen); }
    if (g_Hud.Stats)     { AccentScope s(Hud_Stats); Stats(menuOpen); }
    if (g_Hud.Keybinds)  { AccentScope s(Hud_Keybinds); Keybinds(menuOpen); }
    if (g_Hud.TargetHud) { AccentScope s(Hud_Target); TargetHud(menuOpen); }
    if (g_Hud.Music)     { AccentScope s(Hud_Music); MusicWidget(menuOpen); }
    Hud_RenderToasts();
    PopStyleVar();
}

void Hud_NotifyAlways(const char* icon, const char* text, float seconds) {
    g_toasts.push_back({ icon ? icon : ICON_FA_BELL, text, GetTime(), ImMax(0.5, static_cast<double>(seconds)), true });
    while (g_toasts.size() > 5) g_toasts.pop_front();
}

void Hud_RenderToasts() {
    if (!g_Hud.Notifications) {
        g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(), [](const Toast& t) { return !t.always; }), g_toasts.end());
        if (g_toasts.empty()) return;
    }
    const ImVec4 saved = kit::theme.accent;
    if (g_Hud.NotifyCustomColor) kit::theme.accent = g_Hud.NotifyColor;
    Toasts();
    kit::theme.accent = saved;
}
