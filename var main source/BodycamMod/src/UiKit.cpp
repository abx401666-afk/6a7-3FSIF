#include "../include/MenuInput.hpp"
#include "../include/UiKit.hpp"
#include "../include/MenuUI.hpp"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

namespace { struct ThemeSkip { ImDrawList* dl; int v0, v1; }; std::vector<ThemeSkip> g_themeSkip; }
static void kit_theme_skip(ImDrawList* dl, int v0, int v1) { if (v1 > v0) g_themeSkip.push_back({ dl, v0, v1 }); }

const char* VkName(int vk, char* buf, size_t n);

using namespace ImGui;

namespace kit {
Theme theme;
std::function<void(const char*, bool)> on_toggle;

namespace {
    bool  g_inPage = false;
    bool  g_groupOpen = false;
    float g_pageW = 0.0f, g_colW = 0.0f, g_topY = 0.0f;
    float g_colX[2] = {};
    float g_colBottom = 0.0f;
    bool  g_firstInColumn = true;

    std::string g_filter;
    std::string g_ctx;
    int         g_matchRows = 0;
    std::string g_pendingSection;
    bool  g_pageMatched = false;

    struct Panel {
        std::string id, title, subtitle; std::function<void()> body;
        bool open = false; float t = 0.0f; ImVec2 anchor{}; double openTime = -100.0; float lastH = 260.0f;
        ImVec2 pos{}; bool placed = false;
        float bottomY = -1.0f;
    };
    std::vector<Panel> g_panels;
    bool  g_inPanel = false;

    double g_entranceStart = -100.0;
    int    g_rowIndex = 0;
    float  g_pageAlpha = 1.0f;

    ImGuiID g_capture = 0;
    bool    g_captureArmed = false;

    constexpr float kRowH = 28.0f, kPanelRowH = 24.0f, kRound = 5.0f;

    bool g_compact = false;
    float g_lastMatchBottom = 0.0f;
    float RowH() { return g_inPanel || g_compact ? kPanelRowH : kRowH; }
    bool g_wide = false;
    float RowW() {
        if (g_inPanel || !g_inPage) return GetContentRegionAvail().x;
        if (g_wide) return g_pageW;
        return searching() ? g_pageW : g_colW;
    }

    bool Match(const char* label) {
        if (g_filter.empty()) return true;
        std::string s(label, FindRenderedTextEnd(label));
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s.find(g_filter) != std::string::npos;
    }

    float Anim(ImGuiID id, float target) { return anim(id, target); }
    ImGuiID Sub(ImGuiID id, const char* tag) { return ImHashStr(tag, 0, id); }

    float NextEntrance() {
        if (g_inPanel || !g_inPage || g_compact) return 1.0f;
        const int idx = ImMin(g_rowIndex++, 14);
        const double t = GetTime() - g_entranceStart - idx * 0.022;
        return ease_out(ImSaturate(static_cast<float>(t / 0.22)));
    }
    bool g_disabled = false;
    void ApplyEntrance(float e) {
        if (!g_inPage || g_inPanel) return;
        GImGui->Style.Alpha = g_pageAlpha * (0.0f + e) * (g_disabled ? 0.35f : 1.0f);
    }

    void DrawSectionTitle(const char* title, bool topMargin) {
        ImGuiWindow* w = GetCurrentWindow();
        if (topMargin) ItemSize(ImVec2(0, 6));
        const float e = NextEntrance();
        ApplyEntrance(e);
        const ImVec2 p = w->DC.CursorPos;
        ItemSize(ImVec2(RowW(), 20));
        const float sx = p.x + (1.0f - e) * 8.0f;
        Capsule(w->DrawList, { sx + 0.5f, p.y + 5.5f }, { sx + 3.5f, p.y + 15.5f }, Accent(0.9f));
        Text(w->DrawList, Bold(), 12.0f, { sx + 9, p.y + 3 }, C(226, 226, 234), title);
        const float tw = TextSize(Bold(), 12.0f, title).x;
        if (!g_inPanel) {
            const float lx0 = sx + 9 + tw + 10, lx1 = p.x + RowW() - 2;
            if (lx1 > lx0 + 8) w->DrawList->AddRectFilledMultiColor({ lx0, p.y + 10 }, { lx1, p.y + 11 },
                C(255, 255, 255, 18), C(255, 255, 255, 0), C(255, 255, 255, 0), C(255, 255, 255, 18));
        }
    }

    void FlushHeaders() {
        if (!searching() || g_inPanel) return;
        g_pageMatched = true;
        ImGuiWindow* w = GetCurrentWindow();
        if (!g_ctx.empty()) {
            ItemSize(ImVec2(0, 8));
            const ImVec2 p = w->DC.CursorPos;
            ItemSize(ImVec2(RowW(), 16));
            Text(w->DrawList, Regular(), 11.0f, { p.x + 2, p.y + 1 }, Accent(), g_ctx.c_str());
            g_ctx.clear();
        }
        if (!g_pendingSection.empty()) {
            DrawSectionTitle(g_pendingSection.c_str(), false);
            g_pendingSection.clear();
        }
    }

    struct Row {
        ImRect bb;
        ImGuiID id = 0;
        ImDrawList* dl = nullptr;
        bool hovered = false, held = false, pressed = false;
        ImVec2 mouse;
    };

    bool BeginRow(const char* label, Row& r) {
        ImGuiWindow* w = GetCurrentWindow();
        if (w->SkipItems) return false;
        if (searching() && !g_inPanel && g_inPage && !Match(label)) return false;
        if (searching() && !g_inPanel && g_inPage) ++g_matchRows;
        FlushHeaders();
        const ImVec2 p = w->DC.CursorPos;
        r.bb = ImRect(p, p + ImVec2(RowW(), RowH()));
        r.id = w->GetID(label);
        ItemSize(r.bb.GetSize());  g_lastMatchBottom = w->DC.CursorPos.y;
        if (!ItemAdd(r.bb, r.id)) return false;
        r.pressed = ButtonBehavior(r.bb, r.id, &r.hovered, &r.held, ImGuiButtonFlags_PressedOnClick);
        r.dl = w->DrawList;
        r.mouse = GetIO().MousePos;
        g_firstInColumn = false;

        const float e = NextEntrance();
        ApplyEntrance(e);
        if (e < 1.0f) r.bb.Translate({ (1.0f - e) * 10.0f, 0.0f });

        if (r.hovered) SetMouseCursor(g_disabled ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_Hand);
        if (!g_inPanel) {
            r.dl->AddRectFilled(r.bb.Min, r.bb.Max, C(29, 29, 35), kRound);
            r.dl->AddRect(r.bb.Min, r.bb.Max, C(255, 255, 255, 7), kRound);
        }
        return true;
    }

    void RowLabel(const Row& r, const char* label, bool bright) {
        float x = r.bb.Min.x + 10;
        const float size = g_inPanel || g_compact ? 12.0f : 12.5f;
        if (g_inPanel) {
            const float isz = 10.0f;
            Text(r.dl, Regular(), isz, { x - 2, MidY(Regular(), isz, r.bb.GetCenter().y, ICON_FA_GRIP_VERTICAL) }, C(85, 85, 95), ICON_FA_GRIP_VERTICAL);
            x += 12;
        }
        const float b = anim(Sub(r.id, "##lbl"), (bright || r.hovered) ? 1.0f : 0.0f, 14.0f);
        Text(r.dl, Regular(), size, { x, MidY(Regular(), size, r.bb.GetCenter().y) },
             mix(C(160, 160, 172), C(236, 236, 242), b), label, FindRenderedTextEnd(label));
    }

    ImRect Ellipsis(const Row& r, float rightX) {
        const ImRect bb({ rightX - 18, r.bb.GetCenter().y - 8 }, { rightX, r.bb.GetCenter().y + 8 });
        const bool hov = bb.Contains(r.mouse) && r.hovered;
        const float t = anim(Sub(r.id, "##dots"), hov ? 1.0f : 0.0f, 18.0f, 0.0f);
        const ImVec2 c(std::floor(bb.GetCenter().x) + 0.5f, std::floor(r.bb.GetCenter().y) + 0.5f);
        const ImU32 col = mix(C(150, 150, 162), C(255, 255, 255), t);
        for (int i = -1; i <= 1; ++i) r.dl->AddCircleFilled({ c.x + i * 4.5f, c.y }, 1.45f, col, 12);
        if (hov && !g_disabled) SetMouseCursor(ImGuiMouseCursor_Hand);
        return bb;
    }

    ImRect SwitchRect(const Row& r, float right) {
        return ImRect({ right - 34, r.bb.GetCenter().y - 11 }, { right + 4, r.bb.GetCenter().y + 11 });
    }

    void HostOnlyHint(const Row& r) {
        const float t = anim(Sub(r.id, "##hosttip"), r.hovered ? 1.0f : 0.0f, 16.0f, 0.0f);
        if (r.hovered) SetMouseCursor(ImGuiMouseCursor_Arrow);
        if (t < 0.01f) return;
        const float saved = GImGui->Style.Alpha;
        GImGui->Style.Alpha = g_inPage ? g_pageAlpha : saved;
        tip({ r.mouse.x, r.bb.Min.y - 2 }, ICON_FA_LOCK, "Host only", "Only works in lobbies you host.", ease_out(t));
        GImGui->Style.Alpha = saved;
    }

    ImRect Pill(const Row& r, float rightX, const char* text, bool chevron, ImU32 textCol = 0, float chevOpen = 0.0f) {
        const float fs = 11.5f;
        const ImVec2 ts = TextSize(Regular(), fs, text);
        const float w = ImMax(ts.x + (chevron ? 30.0f : 16.0f), 44.0f);
        const float h = g_inPanel ? 17.0f : 19.0f;
        const float top = ImFloor(r.bb.GetCenter().y - h / 2 + 0.5f);
        const ImRect bb({ ImFloor(rightX - w), top }, { ImFloor(rightX), top + h });
        const bool hov = bb.Contains(r.mouse) && r.hovered;
        const float t = anim(Sub(r.id, "##pill"), hov ? 1.0f : 0.0f, 18.0f, 0.0f);
        r.dl->AddRectFilled(bb.Min, bb.Max, mix(C(40, 40, 49), C(52, 52, 63), t), 4.0f);
        if (t > 0.01f) r.dl->AddRect(bb.Min, bb.Max, Accent(0.35f * t), 4.0f);
        if (chevron) Text(r.dl, Regular(), fs, { bb.Min.x + 8, MidY(Regular(), fs, bb.GetCenter().y) }, textCol ? textCol : C(215, 215, 224), text);
        else {
            const ImVec4 ink = TextInk(Regular(), fs, text);
            const ImVec4 cap = TextInk(Regular(), fs, "H");
            Text(r.dl, Regular(), fs, { ImFloor(bb.GetCenter().x - (ink.x + ink.z) * 0.5f + 0.5f), ImFloor(bb.GetCenter().y - (cap.y + cap.w) * 0.5f) },
                 textCol ? textCol : C(215, 215, 224), text);
        }
        if (chevron) {
            const ImVec2 c(bb.Max.x - 10.5f, bb.GetCenter().y);
            const float k = 1.0f - 2.0f * ImSaturate(chevOpen);
            const ImVec2 pts[3] = { { c.x - 3.2f, c.y - 1.6f * k }, { c.x, c.y + 1.6f * k }, { c.x + 3.2f, c.y - 1.6f * k } };
            r.dl->AddPolyline(pts, 3, mix(C(150, 150, 162), C(230, 230, 238), chevOpen), ImDrawFlags_RoundCornersAll, 1.4f);
        }
        return bb;
    }

    void PopupStyle(bool push) {
        if (push) {
            PushStyleVar(ImGuiStyleVar_WindowPadding, { 5, 5 });
            PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
            PushStyleVar(ImGuiStyleVar_FramePadding, { 8, 3 });
            PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
            PushStyleVar(ImGuiStyleVar_ItemSpacing, { 4, 2 });
            PushStyleVar(ImGuiStyleVar_SelectableTextAlign, { 0.0f, 0.5f });
            PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.105f, 0.105f, 0.13f, 0.99f));
            PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.06f));
            PushStyleColor(ImGuiCol_Header, ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, 0.22f));
            PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.06f));
            PushStyleColor(ImGuiCol_HeaderActive, ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, 0.32f));
            PushFont(Regular(), 12.5f);
        } else {
            PopFont();
            PopStyleColor(5);
            PopStyleVar(6);
        }
    }

    float BeginPopupAnim(ImGuiID pop) {
        const float t = anim(Sub(pop, "##popa"), IsPopupOpen(pop, 0) ? 1.0f : 0.0f, 20.0f, 0.0f);
        PushStyleVar(ImGuiStyleVar_Alpha, GetStyle().Alpha * ease_out(t));
        return ease_out(t);
    }
    void EndPopupAnim() { PopStyleVar(); }

    struct DropState { float t = 0.0f, hy = 0.0f, ha = 0.0f; int hover = -1; ImVec2 pos{}; float w = 0.0f; bool wasOpen = false; };
    DropState& Drop(ImGuiID id) {
        static std::vector<std::pair<ImGuiID, DropState>> states;
        for (auto& e : states) if (e.first == id) return e.second;
        states.push_back({ id, DropState{} });
        return states.back().second;
    }

    template <class SelFn>
    int DropList(ImGuiID pop, const ImRect& pill, const char* const* items, int count, SelFn isSel, bool keepOpen) {
        DropState& st = Drop(pop);
        const bool open = IsPopupOpen(pop, 0);
        if (open && !st.wasOpen) { st.hover = -1; st.ha = 0.0f; }
        st.wasOpen = open;
        const float dt = ImMin(GetIO().DeltaTime, 1.0f / 30.0f);
        st.t = open ? ImMin(1.0f, st.t + dt / 0.20f) : ImMax(0.0f, st.t - dt / 0.13f);
        if (st.t <= 0.0f) return -1;

        float a, sc, dr;
        if (open) {
            const float inv = st.t - 1.0f;
            const float back = 1.0f + 2.2f * inv * inv * inv + 1.2f * inv * inv;
            a = 1.0f - (1.0f - st.t) * (1.0f - st.t) * (1.0f - st.t); sc = 0.94f + 0.06f * back; dr = (1.0f - a) * -6.0f;
        } else {
            const float c = 1.0f - st.t;
            a = 1.0f - c * c * (3.0f - 2.0f * c); a *= a; sc = 1.0f - 0.05f * c * c; dr = c * c * -5.0f;
        }

        constexpr float rowH = 24.0f, pad = 5.0f, fs = 12.5f;
        if (open) {
            float maxTw = 0.0f;
            for (int i = 0; i < count; ++i) maxTw = ImMax(maxTw, TextSize(Regular(), fs, items[i]).x);
            st.w = ImMax(pill.GetWidth(), maxTw + 44.0f);
            st.w = ImMax(st.w, 120.0f);
            st.pos = { pill.Max.x - st.w, pill.Max.y + 5 };
        }
        const float H = pad * 2 + rowH * count;

        PushStyleVar(ImGuiStyleVar_Alpha, GetStyle().Alpha * a);
        PushStyleVar(ImGuiStyleVar_WindowPadding, { pad, pad });
        PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
        PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
        PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
        PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
        PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        SetNextWindowPos(st.pos, ImGuiCond_Always);
        SetNextWindowSize({ st.w, H }, ImGuiCond_Always);
        const ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove;
        bool began;
        if (open) began = BeginPopupEx(pop, fl);
        else {
            char name[32]; std::snprintf(name, sizeof(name), "##ddghost%08X", pop);
            began = Begin(name, nullptr, fl | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
            if (began) BringWindowToDisplayFront(GetCurrentWindow());
        }
        int clicked = -1;
        ImGuiWindow* win = nullptr;
        if (began) {
            win = GetCurrentWindow();
            ImDrawList* dl = win->DrawList;
            const ImVec2 p = win->Pos;
            for (int i = 1; i <= 4; ++i) {
                const float g = i * 2.0f;
                dl->AddRectFilled({ p.x - g, p.y - g + 3 }, { p.x + st.w + g, p.y + H + g + 3 }, C(0, 0, 0, 16 - i * 3), 8.0f + g);
            }
            dl->AddRectFilled(p, { p.x + st.w, p.y + H }, C(27, 27, 33, 252), 8.0f);
            dl->AddRect(p, { p.x + st.w, p.y + H }, C(255, 255, 255, 13), 8.0f);
            const float targetY = st.hover >= 0 ? pad + st.hover * rowH : st.hy;
            st.hy = st.ha < 0.02f ? targetY : ImLerp(st.hy, targetY, ImMin(1.0f, dt * 22.0f));
            st.ha = ImLerp(st.ha, st.hover >= 0 ? 1.0f : 0.0f, ImMin(1.0f, dt * 18.0f));
            if (st.ha > 0.01f)
                dl->AddRectFilled({ p.x + pad, p.y + st.hy }, { p.x + st.w - pad, p.y + st.hy + rowH }, C(255, 255, 255, static_cast<int>(11 * st.ha)), 6.0f);
            int hov = -1;
            for (int i = 0; i < count; ++i) {
                const ImRect bb({ p.x + pad, p.y + pad + i * rowH }, { p.x + st.w - pad, p.y + pad + (i + 1) * rowH });
                const bool sel = isSel(i);
                if (open) {
                    const ImGuiID iid = win->GetID(i);
                    ItemSize(bb.GetSize());
                    if (ItemAdd(bb, iid)) {
                        bool h = false, held = false;
                        if (ButtonBehavior(bb, iid, &h, &held)) clicked = i;
                        if (h) { hov = i; SetMouseCursor(ImGuiMouseCursor_Hand); }
                    }
                }
                const float sa = anim(Sub(pop, items[i]) ^ static_cast<ImGuiID>(i * 2654435761u), sel ? 1.0f : 0.0f, 16.0f);
                Text(dl, Regular(), fs, { bb.Min.x + 9, MidY(Regular(), fs, bb.GetCenter().y) },
                     mix(mix(C(178, 178, 190), C(240, 240, 246), (i == st.hover) ? 1.0f : 0.0f), Accent(), sa), items[i]);
                if (sa > 0.01f) {
                    const ImVec2 c(bb.Max.x - 13, bb.GetCenter().y);
                    const float k = 0.6f + 0.4f * sa;
                    const ImVec2 tk[3] = { c + ImVec2(-3.6f, 0.2f) * k, c + ImVec2(-1.0f, 2.8f) * k, c + ImVec2(4.0f, -2.6f) * k };
                    dl->AddPolyline(tk, 3, Accent(sa), ImDrawFlags_None, 1.6f);
                }
            }
            if (open) st.hover = hov;
            if (clicked >= 0 && !keepOpen) CloseCurrentPopup();
            if (open) EndPopup(); else End();
        }
        PopStyleColor(2);
        PopStyleVar(7);
        if (win && (sc != 1.0f || dr != 0.0f)) {
            const ImVec2 o(st.pos.x + st.w * 0.5f, st.pos.y);
            for (ImDrawVert& v : win->DrawList->VtxBuffer) { v.pos.x = o.x + (v.pos.x - o.x) * sc; v.pos.y = o.y + (v.pos.y - o.y) * sc + dr; }
            for (ImDrawCmd& cmd : win->DrawList->CmdBuffer) {
                cmd.ClipRect.x = o.x + (cmd.ClipRect.x - o.x) * sc - 16; cmd.ClipRect.z = o.x + (cmd.ClipRect.z - o.x) * sc + 16;
                cmd.ClipRect.y = o.y + (cmd.ClipRect.y - o.y) * sc + dr - 16; cmd.ClipRect.w = o.y + (cmd.ClipRect.w - o.y) * sc + dr + 16;
            }
        }
        return clicked;
    }
    float DropOpenT(ImGuiID pop) { return ease_out(Drop(pop).t); }

    bool ColorPopup(ImGuiID popupId, ImVec4* c) {
        bool changed = false;
        BeginPopupAnim(popupId);
        PopupStyle(true);
        PushStyleVar(ImGuiStyleVar_WindowPadding, { 8, 8 });
        if (BeginPopupEx(popupId, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
            SetNextItemWidth(170);
            const int vtx0 = GetWindowDrawList()->VtxBuffer.Size;
            struct SkipMark { ImDrawList* dl; int v0; ~SkipMark() { kit_theme_skip(dl, v0, dl->VtxBuffer.Size); } } mark{ GetWindowDrawList(), vtx0 };
            changed = ColorPicker4("##pick", &c->x,
                ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_AlphaBar |
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoLabel);
            EndPopup();
        }
        PopStyleVar();
        PopupStyle(false);
        EndPopupAnim();
        return changed;
    }

    ImRect Swatch(const Row& r, float rightX, const ImVec4& c) {
        ImRect bb({ rightX - 16, r.bb.GetCenter().y - 8 }, { rightX, r.bb.GetCenter().y + 8 });
        const float t = anim(Sub(r.id, "##sw"), bb.Contains(r.mouse) && r.hovered ? 1.0f : 0.0f, 18.0f, 0.0f);
        const ImRect hit = bb;
        bb.Expand(1.5f * t);
        r.dl->AddRectFilled(bb.Min, bb.Max, C(255, 255, 255, static_cast<int>(30 + 50 * t)), 4.0f);
        r.dl->AddRectFilled(bb.Min + ImVec2(1, 1), bb.Max - ImVec2(1, 1),
                            GetColorU32(ImVec4(c.x, c.y, c.z, ImMax(c.w, 0.25f))), 3.5f);
        return hit;
    }
}

ImU32 C(int r, int g, int b, int a) {
    return GetColorU32(ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f));
}
void Capsule(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) {
    const float w = b.x - a.x, h = b.y - a.y;
    if (w <= 0.0f || h <= 0.0f) return;
    const float r = (w < h ? w : h) * 0.5f;
    if (h >= w) {
        const float cx = a.x + r;
        dl->PathArcTo({ cx, a.y + r }, r, IM_PI, 2.0f * IM_PI, 10);
        dl->PathArcTo({ cx, b.y - r }, r, 0.0f, IM_PI, 10);
    } else {
        const float cy = a.y + r;
        dl->PathArcTo({ b.x - r, cy }, r, -0.5f * IM_PI, 0.5f * IM_PI, 10);
        dl->PathArcTo({ a.x + r, cy }, r, 0.5f * IM_PI, 1.5f * IM_PI, 10);
    }
    dl->PathFillConvex(col);
}

bool MacClose(const char* id, ImVec2 c, ImDrawList* dl) {
    if (!dl) dl = GetWindowDrawList();
    SetCursorScreenPos({ c.x - 9.0f, c.y - 9.0f });
    const bool clicked = InvisibleButton(id, { 18, 18 });
    const bool hov = IsItemHovered(), held = IsItemActive();
    const float r = 6.0f;
    dl->AddCircleFilled(c, r, held ? C(226, 70, 63) : C(226, 70, 63), 32);
    dl->AddCircleFilled(c, r - 0.5f, held ? C(191, 73, 67) : C(255, 95, 87), 32);
    if (hov || held) {
        const ImU32 xc = held ? C(51, 0, 0) : C(77, 0, 0);
        const float a = 2.75f;
        dl->PathLineTo({ c.x - a, c.y - a }); dl->PathLineTo({ c.x + a, c.y + a }); dl->PathStroke(xc, 0, 1.15f);
        dl->PathLineTo({ c.x - a, c.y + a }); dl->PathLineTo({ c.x + a, c.y - a }); dl->PathStroke(xc, 0, 1.15f);
    }
    return clicked;
}

bool AccentIsLight() {
    const ImVec4& a = theme.accent;
    return 0.2126f * a.x + 0.7152f * a.y + 0.0722f * a.z > 0.62f;
}
ImU32 OnAccent(float alpha) {
    return AccentIsLight() ? C(21, 20, 26, static_cast<int>(255 * alpha)) : C(255, 255, 254, static_cast<int>(255 * alpha));
}

ImU32 Accent(float alpha) {
    return GetColorU32(ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, theme.accent.w * alpha));
}
ImFont* Regular() { return ui::font(0); }
ImFont* Bold() { return ui::font(1); }

void Text(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const char* s, const char* e) {
    dl->AddText(f, size, ImVec2(ImFloor(pos.x), ImFloor(pos.y)), col, s, e);
}
ImVec2 TextSize(ImFont* f, float size, const char* s, const char* e) {
    return f->CalcTextSizeA(size, FLT_MAX, 0.0f, s, e);
}
ImVec4 TextInk(ImFont* f, float size, const char* s, const char* e) {
    ImFontBaked* b = f->GetFontBaked(size);
    const float sc = size / b->Size;
    float x = 0.0f, x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
    if (!e) e = s + std::strlen(s);
    for (const char* q = s; q < e;) {
        unsigned int ch = 0;
        q += ImTextCharFromUtf8(&ch, q, e);
        const ImFontGlyph* g = b->FindGlyph(static_cast<ImWchar>(ch));
        if (!g) continue;
        if (g->Visible) {
            x0 = ImMin(x0, x + g->X0 * sc); x1 = ImMax(x1, x + g->X1 * sc);
            y0 = ImMin(y0, g->Y0 * sc);     y1 = ImMax(y1, g->Y1 * sc);
        }
        x += g->AdvanceX * sc;
    }
    if (x0 > x1) { const ImVec2 ts = TextSize(f, size, s, e); return { 0, 0, ts.x, ts.y }; }
    return { x0, y0, x1, y1 };
}
float MidY(ImFont* f, float size, float cy, const char* ink) {
    const ImVec4 b = TextInk(f, size, ink ? ink : "H");
    return ImFloor(cy - (b.y + b.w) * 0.5f + 0.5f);
}
void TextCentered(ImDrawList* dl, ImFont* f, float size, ImVec2 c, ImU32 col, const char* s, const char* e) {
    const ImVec4 ink = TextInk(f, size, s, e);
    dl->AddText(f, size, ImVec2(ImFloor(c.x - (ink.x + ink.z) * 0.5f + 0.5f), ImFloor(c.y - (ink.y + ink.w) * 0.5f + 0.5f)), col, s, e);
}

void Card(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float alpha) {
    dl->AddRectFilled(a, b, C(21, 21, 26, static_cast<int>(240 * alpha)), rounding);
    dl->AddRect(a, b, C(255, 255, 255, static_cast<int>(12 * alpha)), rounding);
}

void Toggle(ImDrawList* dl, ImVec2 p, float t) {
    const ImVec2 sz(30, 16);
    const ImU32 off = C(56, 56, 66);
    ImVec4 offV = ColorConvertU32ToFloat4(off);
    ImVec4 onV(theme.accent.x, theme.accent.y, theme.accent.z, offV.w);
    dl->AddRectFilled(p, p + sz, ColorConvertFloat4ToU32(ImLerp(offV, onV, t)), sz.y / 2);
    const float r = 5.5f;
    const ImVec2 c(p.x + 8.0f + (sz.x - 16.0f) * t, p.y + sz.y / 2);
    const ImU32 knob = mix(C(165, 165, 165), OnAccent(), t);
    dl->AddCircleFilled(c + ImVec2(0, 0.8f), r + 0.6f, C(0, 0, 0, 70), 20);
    dl->AddCircleFilled(c, r, knob, 20);
}

float anim(ImGuiID id, float target, float speed, float init) {
    float* t = GetStateStorage()->GetFloatRef(id, init < 0.0f ? target : init);
    *t = ImLerp(*t, target, ImMin(1.0f, GetIO().DeltaTime * speed));
    if (std::fabs(*t - target) < 0.002f) *t = target;
    return *t;
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = ColorConvertU32ToFloat4(a), y = ColorConvertU32ToFloat4(b);
    return ColorConvertFloat4ToU32(ImLerp(x, y, ImSaturate(t)));
}

void tip(ImVec2 anchor, const char* icon, const char* title, const char* line, float t) {
    if (t < 0.01f) return;
    ImDrawList* fg = GetForegroundDrawList();
    const ImVec2 is = icon ? TextSize(Regular(), 11.0f, icon) : ImVec2(0, 0);
    const ImVec2 ts = TextSize(Bold(), 12.0f, title);
    const ImVec2 ls = line ? TextSize(Regular(), 11.0f, line) : ImVec2(0, 0);
    const float iconW = icon ? 26.0f : 0.0f;
    const float w = 12 + iconW + ImMax(ts.x, ls.x) + 14, h = line ? 44.0f : 28.0f;
    const float lift = (1.0f - t) * 4.0f;
    const ImVec2 ds = GetIO().DisplaySize;
    ImVec2 a(std::floor(anchor.x - w * 0.5f), std::floor(anchor.y - h - 8 + lift));
    a.x = ImClamp(a.x, 4.0f, ds.x - w - 4.0f);
    const ImVec2 b(a.x + w, a.y + h);
    const float al = GetStyle().Alpha * t;
    auto A = [al](ImU32 c) { return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(((c >> IM_COL32_A_SHIFT) & 0xFF) * al) << IM_COL32_A_SHIFT); };
    for (int i = 1; i <= 3; ++i) fg->AddRectFilled({ a.x - i * 1.5f, a.y - i * 1.5f + 2 }, { b.x + i * 1.5f, b.y + i * 1.5f + 2 }, A(IM_COL32(0, 0, 0, 30 - i * 8)), 8.0f + i);
    fg->AddRectFilled(a, b, A(IM_COL32(28, 28, 35, 252)), 8.0f);
    fg->AddRect(a, b, A(IM_COL32(255, 255, 255, 16)), 8.0f);
    float x = a.x + 12;
    if (icon) {
        const ImVec2 tc(x + 9, (a.y + b.y) * 0.5f);
        fg->AddCircleFilled(tc, 10.0f, A(Accent(0.18f)), 20);
        Text(fg, Regular(), 11.0f, tc - is * 0.5f, A(Accent()), icon);
        x += iconW;
    }
    if (line) {
        Text(fg, Bold(), 12.0f, { x, a.y + 7 }, A(IM_COL32(240, 240, 246, 255)), title);
        Text(fg, Regular(), 11.0f, { x, a.y + 24 }, A(IM_COL32(150, 150, 164, 255)), line);
    } else {
        Text(fg, Bold(), 12.0f, { x, (a.y + b.y) * 0.5f - ts.y * 0.5f }, A(IM_COL32(240, 240, 246, 255)), title);
    }
}

float ease_out(float t) { t = ImSaturate(t); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }

void restart_entrance() { g_entranceStart = GetTime(); }

const char* KeyName(int vk) {
    static char buf[64];
    return VkName(vk, buf, sizeof(buf));
}

void begin_page(float width) {
    g_inPage = true;
    g_pageW = width;
    g_colW = (width - 12.0f) * 0.5f;
    g_topY = GetCursorPosY();
    g_colX[0] = GetCursorPosX();
    g_colX[1] = g_colX[0] + g_colW + 12.0f;

    g_groupOpen = false;
    g_firstInColumn = true;
    g_pendingSection.clear();
    g_pageMatched = false;
    g_rowIndex = 0;
    g_colBottom = 0.0f;
    g_pageAlpha = GetStyle().Alpha;
    PushStyleVar(ImGuiStyleVar_ItemSpacing, { 8, 3 });
}

void column(int index) {
    if (searching()) return;
    if (g_groupOpen) { EndGroup(); g_colBottom = ImMax(g_colBottom, GetItemRectMax().y); }

    g_rowIndex = 0;
    SetCursorPos({ g_colX[index], g_topY });
    BeginGroup();
    g_groupOpen = true;
    g_firstInColumn = true;
}

void end_page() {
    if (g_groupOpen) EndGroup();
    g_groupOpen = false;
    PopStyleVar();
    GImGui->Style.Alpha = g_pageAlpha;
    g_inPage = false;
    g_disabled = false;
    g_wide = false;
    g_ctx.clear();
    g_pendingSection.clear();
}

void section(const char* title) {
    if (searching() && !g_inPanel) { g_pendingSection = title; return; }
    DrawSectionTitle(title, !g_firstInColumn);
    g_firstInColumn = false;
}

void spacing(float px) {
    ImGuiWindow* w = GetCurrentWindow();
    if (g_inPanel) {
        const ImVec2 p = w->DC.CursorPos;
        ItemSize(ImVec2(0, px));
        w->DrawList->AddLine({ p.x, p.y + px * 0.5f }, { p.x + GetContentRegionAvail().x, p.y + px * 0.5f }, C(255, 255, 255, 14));
    } else if (!searching()) {
        ItemSize(ImVec2(0, px));
    }
}

void set_filter(const char* text) {
    g_filter = text ? text : "";
    std::transform(g_filter.begin(), g_filter.end(), g_filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    while (!g_filter.empty() && g_filter.back() == ' ') g_filter.pop_back();
    if (!g_filter.empty()) g_matchRows = 0;
}
int search_match_count() { return g_matchRows; }
bool searching() { return !g_filter.empty(); }
void set_search_context(const char* path) { g_ctx = path; }
bool page_had_matches() { return g_pageMatched; }

void set_disabled(bool on) { g_disabled = on; }
void compact_rows(bool on) { g_compact = on; }

bool toggle(const char* label, bool* v, const char* settings) {
    Row r;
    if (!BeginRow(label, r)) return false;
    if (g_disabled) { r.pressed = false; HostOnlyHint(r); }
    const float t = Anim(r.id, *v ? 1.0f : 0.0f);
    const float right = r.bb.Max.x - 10;
    Toggle(r.dl, { right - 30, r.bb.GetCenter().y - 8.0f }, t);
    const ImRect sw = SwitchRect(r, right);

    ImRect dots;
    if (settings) dots = Ellipsis(r, right - 38);
    RowLabel(r, label, *v);
    if (g_disabled && r.hovered) SetMouseCursor(ImGuiMouseCursor_Arrow);

    if (r.pressed) {
        if (settings && dots.Contains(r.mouse)) { open_panel(settings); return false; }
        if (!sw.Contains(r.mouse)) return false;
        *v = !*v;
        if (on_toggle && !g_inPanel) on_toggle(label, *v);
        return true;
    }
    return false;
}

bool toggle_color(const char* label, bool* v, ImVec4* c, const char* settings) {
    Row r;
    if (!BeginRow(label, r)) return false;
    const float t = Anim(r.id, *v ? 1.0f : 0.0f);
    const float right = r.bb.Max.x - 10;
    Toggle(r.dl, { right - 30, r.bb.GetCenter().y - 8.0f }, t);
    const ImRect sw = Swatch(r, right - 38, *c);
    ImRect dots;
    if (settings) dots = Ellipsis(r, sw.Min.x - 6);
    RowLabel(r, label, *v);

    const ImGuiID pop = ImHashStr("##col", 0, r.id);
    bool changed = false;
    if (r.pressed) {
        if (sw.Contains(r.mouse)) OpenPopupEx(pop);
        else if (settings && dots.Contains(r.mouse)) open_panel(settings);
        else if (SwitchRect(r, right).Contains(r.mouse)) { *v = !*v; changed = true; if (on_toggle && !g_inPanel) on_toggle(label, *v); }
    }
    SetNextWindowPos({ sw.Max.x, sw.Max.y + 4 }, ImGuiCond_Appearing, { 1.0f, 0.0f });
    changed |= ColorPopup(pop, c);
    return changed;
}

bool slider(const char* label, float* v, float mn, float mx, const char* fmt, const char* settings) {
    Row r;
    if (!BeginRow(label, r)) return false;
    const float right = r.bb.Max.x - 10;
    const float trackW = g_compact ? 46.0f : g_inPanel ? 66.0f : 80.0f;
    const ImRect track({ right - trackW, r.bb.GetCenter().y - 1.5f }, { right, r.bb.GetCenter().y + 1.5f });
    const ImRect grab(track.Min - ImVec2(6, 9), track.Max + ImVec2(6, 9));

    bool* dragging = reinterpret_cast<bool*>(GetStateStorage()->GetBoolRef(ImHashStr("##drag", 0, r.id), false));
    if (r.pressed && grab.Contains(r.mouse)) *dragging = true;
    if (!r.held) *dragging = false;

    bool changed = false;
    if (*dragging) {
        const float f = ImSaturate((r.mouse.x - track.Min.x) / track.GetWidth());
        const float nv = mn + (mx - mn) * f;
        if (nv != *v) { *v = nv; changed = true; }
    }

    const float frac = ImSaturate((*v - mn) / (mx - mn));
    const float shown = Anim(r.id, frac);
    const float kh = anim(Sub(r.id, "##knob"), *dragging ? 2.0f : (grab.Contains(r.mouse) && r.hovered) ? 1.0f : 0.0f, 18.0f, 0.0f);
    const float th = 5.0f + 0.5f * kh, cy = track.GetCenter().y;
    const ImVec2 ta(track.Min.x, cy - th * 0.5f), tb(track.Max.x, cy + th * 0.5f);
    Capsule(r.dl, ta, tb, C(46, 46, 56));
    const float fx = ta.x + (tb.x - ta.x) * shown;
    if (fx - ta.x > 0.5f)
        Capsule(r.dl, ta, { ImMax(fx, ta.x + th), tb.y }, mix(Accent(0.92f), Accent(1.0f), ImMin(1.0f, kh)));

    char buf[48];
    std::snprintf(buf, sizeof(buf), fmt, *v);
    const float fs = 12.0f;
    const ImVec2 ts = TextSize(Regular(), fs, buf);
    const float textX = track.Min.x - 12 - ts.x;
    Text(r.dl, Regular(), fs, { textX, MidY(Regular(), fs, r.bb.GetCenter().y) },
         mix(C(215, 215, 224), C(255, 255, 255), ImMin(1.0f, kh)), buf);

    if (settings) {
        const ImRect dots = Ellipsis(r, textX - 8);
        if (r.pressed && dots.Contains(r.mouse)) open_panel(settings);
    }
    RowLabel(r, label, false);
    return changed;
}

bool segmented(const char* label, int* v, const char* const* items, int count) {
    Row r;
    if (!BeginRow(label, r)) return false;
    RowLabel(r, label, false);
    count = ImClamp(count, 1, 4);
    const float fs = 11.5f, h = g_inPanel ? 18.0f : 20.0f, pad = 2.0f;
    float segW = 0.0f;
    for (int i = 0; i < count; ++i) segW = ImMax(segW, TextSize(Regular(), fs, items[i]).x + 18.0f);
    segW = ImFloor(ImMax(segW, 44.0f));
    const float w = segW * count + pad * 2;
    const ImRect bb({ ImFloor(r.bb.Max.x - 10 - w), ImFloor(r.bb.GetCenter().y - h * 0.5f) }, { ImFloor(r.bb.Max.x - 10), ImFloor(r.bb.GetCenter().y - h * 0.5f) + h });
    r.dl->AddRectFilled(bb.Min, bb.Max, C(36, 36, 44), 5.0f);
    const int cur = ImClamp(*v, 0, count - 1);
    const float sx = anim(Sub(r.id, "##seg"), static_cast<float>(cur), 18.0f);
    const ImVec2 ta(bb.Min.x + pad + sx * segW, bb.Min.y + pad);
    r.dl->AddRectFilled(ta, { ta.x + segW, bb.Max.y - pad }, C(62, 62, 74), 4.0f);
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        const ImRect sb({ bb.Min.x + pad + i * segW, bb.Min.y }, { bb.Min.x + pad + (i + 1) * segW, bb.Max.y });
        const bool hov = sb.Contains(r.mouse) && r.hovered;
        if (hov && !g_disabled) SetMouseCursor(ImGuiMouseCursor_Hand);
        if (hov && r.pressed && i != cur && !g_disabled) { *v = i; changed = true; }
        const float on = ImSaturate(1.0f - ImFabs(sx - i));
        const ImVec2 ts = TextSize(Regular(), fs, items[i]);
        Text(r.dl, Regular(), fs, { ImFloor(sb.GetCenter().x - ts.x * 0.5f), MidY(Regular(), fs, bb.GetCenter().y) },
             mix(hov ? C(200, 200, 212) : C(140, 140, 152), C(242, 242, 248), on), items[i]);
    }
    return changed;
}

bool combo(const char* label, int* cur, const char* const* items, int count) {
    Row r;
    if (!BeginRow(label, r)) return false;
    const int idx = ImClamp(*cur, 0, count - 1);
    const ImGuiID pop = ImHashStr("##combo", 0, r.id);
    const ImRect pill = Pill(r, r.bb.Max.x - 10, items[idx], true, 0, DropOpenT(pop));
    RowLabel(r, label, false);
    if (r.pressed) OpenPopupEx(pop);
    const int hit = DropList(pop, pill, items, count, [&](int i) { return i == idx; }, false);
    if (hit >= 0 && hit != *cur) { *cur = hit; return true; }
    return false;
}

bool multi(const char* label, unsigned* mask, const char* const* items, int count) {
    Row r;
    if (!BeginRow(label, r)) return false;
    std::string text;
    int n = 0, first = -1;
    for (int i = 0; i < count; ++i) if (*mask & (1u << i)) { if (first < 0) first = i; ++n; }
    if (n == 0) text = "None";
    else { text = items[first]; if (n > 1) text += ", +" + std::to_string(n - 1); }
    const ImGuiID pop = ImHashStr("##multi", 0, r.id);
    const ImRect pill = Pill(r, r.bb.Max.x - 10, text.c_str(), true, 0, DropOpenT(pop));
    RowLabel(r, label, false);
    if (r.pressed) OpenPopupEx(pop);
    const unsigned m = *mask;
    const int hit = DropList(pop, pill, items, count, [&](int i) { return (m & (1u << i)) != 0; }, true);
    if (hit >= 0) { *mask ^= 1u << hit; return true; }
    return false;
}

bool keybind(const char* label, int* vk) {
    Row r;
    if (!BeginRow(label, r)) return false;
    const bool capturing = g_capture == r.id;
    const float pulse = capturing ? 0.55f + 0.45f * std::sin(static_cast<float>(GetTime()) * 7.0f) : 1.0f;
    const ImRect pill = Pill(r, r.bb.Max.x - 10, capturing ? "press a key" : KeyName(*vk), false,
                             capturing ? Accent(pulse) : 0);
    RowLabel(r, label, false);

    bool changed = false;
    if (r.pressed && pill.Contains(r.mouse) && !capturing) {
        g_capture = r.id;
        g_captureArmed = false;
    } else if (capturing) {
        if (IsMouseClicked(ImGuiMouseButton_Left) && !pill.Contains(r.mouse) && g_captureArmed) {
            g_capture = 0;
        } else {
            bool anyDown = false;
            int found = 0;
            for (int k = 1; k < 255; ++k) {
                if (k == VK_INSERT || k == VK_ESCAPE) continue;
                if (MenuInput_KeyDown(k)) { anyDown = true; if (!found) found = k; }
            }
            if (MenuInput_KeyDown(VK_ESCAPE)) { *vk = 0; changed = true; g_capture = 0; }
            else if (!g_captureArmed) g_captureArmed = !anyDown;
            else if (found) { *vk = found; changed = true; g_capture = 0; }
        }
    }
    return changed;
}

bool color(const char* label, ImVec4* c) {
    Row r;
    if (!BeginRow(label, r)) return false;
    const ImRect sw = Swatch(r, r.bb.Max.x - 10, *c);
    RowLabel(r, label, false);
    const ImGuiID pop = ImHashStr("##col", 0, r.id);
    if (r.pressed) OpenPopupEx(pop);
    SetNextWindowPos({ sw.Max.x, sw.Max.y + 4 }, ImGuiCond_Appearing, { 1.0f, 0.0f });
    return ColorPopup(pop, c);
}

void value_row(const char* label, const char* value, ImU32 valueCol) {
    Row r;
    if (!BeginRow(label, r)) return;
    if (r.hovered) SetMouseCursor(ImGuiMouseCursor_Arrow);
    const float fs = 12.0f;
    const ImVec2 ts = TextSize(Regular(), fs, value);
    Text(r.dl, Regular(), fs, { r.bb.Max.x - 10 - ts.x, MidY(Regular(), fs, r.bb.GetCenter().y) },
         valueCol ? valueCol : C(215, 215, 224), value);
    RowLabel(r, label, false);
}

bool button_row(const char* label, const char* button) {
    Row r;
    if (!BeginRow(label, r)) return false;
    const ImRect pill = Pill(r, r.bb.Max.x - 10, button, false, Accent());
    RowLabel(r, label, false);
    return r.pressed && pill.Contains(r.mouse);
}

void note(const char* text) {
    if (searching() && !g_inPanel) return;
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return;
    const float e = NextEntrance();
    ApplyEntrance(e);
    const ImVec2 p = w->DC.CursorPos;
    const float nx = p.x + 4 + (1.0f - e) * 10.0f;
    const float wrapW = ImMax(40.0f, RowW() - 23.0f);
    const char* s = text; const char* end = text + std::strlen(text);
    int lines = 0;
    while (s < end && lines < 6) {
        const char* brk = Regular()->CalcWordWrapPosition(11.0f, s, end, wrapW);
        if (brk == s) brk = s + 1;
        Text(w->DrawList, Regular(), 11.0f, { nx + 15, p.y + 1 + lines * 14.0f }, C(118, 118, 132), s, brk);
        s = brk;
        while (s < end && (*s == ' ' || *s == '\n')) ++s;
        ++lines;
    }
    ItemSize(ImVec2(RowW(), 16 + ImMax(0, lines - 1) * 14.0f));
    Text(w->DrawList, Regular(), 9.5f, { nx, p.y + 2.5f }, C(95, 95, 110), ICON_FA_CIRCLE_INFO);
}

static std::string g_cfgRenaming;
static char        g_cfgRenameBuf[64] = {};
static bool        g_cfgRenameFocus = false;
const char* config_card_rename_text() { return g_cfgRenameBuf; }

int config_card(const char* name, const char* sub, bool selected, bool active, bool dirty, bool autoload, bool deleteArmed) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return ConfigCard_None;
    if (searching() && g_inPage && !Match(name)) return ConfigCard_None;
    if (searching() && g_inPage) ++g_matchRows;
    FlushHeaders();
    const ImVec2 p = w->DC.CursorPos;
    const float H = 50.0f;
    const ImRect bb(p, p + ImVec2(RowW(), H));
    PushID(name);
    const ImGuiID gid = w->GetID("##card");
    ItemSize(ImVec2(bb.GetWidth(), H + 2));  g_lastMatchBottom = w->DC.CursorPos.y;
    if (!ItemAdd(bb, gid)) { PopID(); return ConfigCard_None; }
    g_firstInColumn = false;
    const ImVec2 after = w->DC.CursorPos, prevLine = w->DC.CursorPosPrevLine;
    const ImVec2 prevSize = w->DC.PrevLineSize, currSize = w->DC.CurrLineSize;
    ImDrawList* dl = w->DrawList;
    ApplyEntrance(NextEntrance());
    static ImDrawListSplitter split;
    split.Split(dl, 3);
    split.SetCurrentChannel(dl, 2);

    const bool cardHov = IsMouseHoveringRect(bb.Min, bb.Max) && IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool renaming = g_cfgRenaming == name;
    const float showT = anim(Sub(gid, "##act"), cardHov && !renaming ? 1.0f : 0.0f, 24.0f, 0.0f);
    struct Act { const char* icon; const char* text; int code; const char* tip; const char* tipLine; };
    constexpr int kRenameAct = 100;
    const Act acts[4] = {
        { ICON_FA_FILE_IMPORT, "Load", ConfigCard_Load, "Load", "Apply this config now" },
        { ICON_FA_FLOPPY_DISK, "Save", ConfigCard_Save, "Save", "Overwrite it with your current settings" },
        { ICON_FA_PEN, nullptr, kRenameAct, "Rename", "Type a new name, Enter to save" },
        { ICON_FA_TRASH, nullptr, ConfigCard_Delete, deleteArmed ? "Click again to delete" : "Delete", deleteArmed ? nullptr : "Asks once more before deleting" } };
    const float bh = 26.0f, gap = 5.0f;
    int result = ConfigCard_None;
    float bx = bb.Max.x - 10;
    ImRect actRect(bb.Max, bb.Max);
    ImRect rects[4];
    const bool narrow = g_compact || bb.GetWidth() < 260.0f;
    if (narrow) {
        const float m = 10.0f, ng = 6.0f;
        const float ew = std::floor((bb.GetWidth() - 2 * m - 3 * ng) / 4.0f);
        const float x0 = std::floor(bb.Min.x + (bb.GetWidth() - (4 * ew + 3 * ng)) * 0.5f);
        for (int i = 0; i < 4; ++i) {
            rects[i] = ImRect({ x0 + i * (ew + ng), bb.GetCenter().y - bh / 2 }, { x0 + i * (ew + ng) + ew, bb.GetCenter().y + bh / 2 });
            actRect.Add(rects[i]);
        }
        actRect.Min.x = rects[0].Min.x;
    } else {
        for (int i = 3; i >= 0; --i) {
            const float bw = acts[i].text ? std::floor(TextSize(Regular(), 11.5f, acts[i].text).x + TextSize(Regular(), 11.0f, acts[i].icon).x + 24.0f) : bh;
            rects[i] = ImRect({ bx - bw, bb.GetCenter().y - bh / 2 }, { bx, bb.GetCenter().y + bh / 2 });
            bx -= bw + gap;
            actRect.Add(rects[i]);
        }
    }
    {
        const float tSel = Anim(gid, selected ? 1.0f : 0.0f);
        const float hvb = anim(Sub(gid, "##hov"), cardHov ? 1.0f : 0.0f, 16.0f, 0.0f);
        const ImU32 base = mix(mix(C(29, 29, 35), C(34, 34, 42), hvb), C(35, 33, 50), tSel);
        if (showT > 0.01f) {
            const ImU32 solid = (base & ~IM_COL32_A_MASK) | (static_cast<ImU32>(255 * showT) << IM_COL32_A_SHIFT);
            const ImU32 clear = base & ~IM_COL32_A_MASK;
            if (narrow) dl->AddRectFilled({ bb.Min.x + 1, bb.Min.y + 1 }, { bb.Max.x - 1, bb.Max.y - 1 }, solid, 7.0f);
            else {
                const float x0 = actRect.Min.x - 26, x1 = actRect.Min.x - 4;
                dl->AddRectFilledMultiColor({ x0, bb.Min.y + 1 }, { x1, bb.Max.y - 1 }, clear, solid, solid, clear);
                dl->AddRectFilled({ x1, bb.Min.y + 1 }, { bb.Max.x - 1, bb.Max.y - 1 }, solid, 7.0f, ImDrawFlags_RoundCornersRight);
            }
        }
    }
    for (int i = 3; i >= 0; --i) {
        const ImRect r = rects[i];
        if (showT < 0.02f || renaming) continue;
        SetCursorScreenPos(r.Min);
        PushID(i);
        const bool clicked = InvisibleButton("##a", r.GetSize());
        const bool h = IsItemHovered();
        PopID();
        if (h) SetMouseCursor(ImGuiMouseCursor_Hand);
        const float ht = anim(GetItemID(), h ? 1.0f : 0.0f, 18.0f, 0.0f);
        const bool danger = acts[i].code == ConfigCard_Delete;
        const bool primary = acts[i].code == ConfigCard_Load;
        auto F = [showT](ImU32 c) { return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(((c >> 24) & 0xFF) * showT) << 24); };
        ImU32 bg, bd = 0, fg;
        if (danger) { bg = deleteArmed ? C(255, 90, 90, 70) : mix(C(255, 90, 90, 14), C(255, 90, 90, 34), ht); bd = C(255, 95, 95, static_cast<int>(40 + 60 * ht)); fg = C(255, 120, 110); }
        else if (primary) { bg = mix(Accent(0.85f), Accent(1.0f), ht); fg = OnAccent(); }
        else { bg = mix(C(40, 40, 50), C(50, 50, 62), ht); bd = mix(C(255, 255, 255, 12), Accent(0.45f), ht); fg = C(230, 230, 238); }
        dl->AddRectFilled(r.Min, r.Max, F(bg), 7.0f);
        if (bd) dl->AddRect(r.Min, r.Max, F(bd), 7.0f);
        const ImVec2 is = TextSize(Regular(), 11.0f, acts[i].icon);
        if (acts[i].text && !narrow) {
            const ImVec2 ts = TextSize(Regular(), 11.5f, acts[i].text);
            float x = std::floor(r.GetCenter().x - (is.x + 6 + ts.x) * 0.5f);
            Text(dl, Regular(), 11.0f, { x, MidY(Regular(), 11.0f, r.GetCenter().y, acts[i].icon) }, F(primary ? fg : Accent()), acts[i].icon);
            Text(dl, Regular(), 11.5f, { x + is.x + 6, MidY(Regular(), 11.5f, r.GetCenter().y) }, F(fg), acts[i].text);
        } else {
            TextCentered(dl, Regular(), 11.0f, r.GetCenter(), F(fg), acts[i].icon);
        }
        const float tt = anim(Sub(GetItemID(), "##tip"), h ? 1.0f : 0.0f, h ? 12.0f : 1000.0f, 0.0f);
        if (h && GetCurrentContext()->HoveredIdTimer > 0.25f) tip({ r.GetCenter().x, r.Min.y }, acts[i].icon, acts[i].tip, acts[i].tipLine, ease_out(tt));
        if (clicked) {
            if (acts[i].code == kRenameAct) {
                g_cfgRenaming = name;
                std::snprintf(g_cfgRenameBuf, sizeof(g_cfgRenameBuf), "%s", name);
                g_cfgRenameFocus = true;
            } else result = acts[i].code;
        }
    }
    SetCursorScreenPos(bb.Min);
    const bool bodyHov = cardHov && !(showT > 0.02f && actRect.Contains(GetIO().MousePos));
    if (bodyHov && IsMouseClicked(0) && result == ConfigCard_None) result = ConfigCard_Select;
    if (bodyHov && IsMouseDoubleClicked(0) && !renaming) result = ConfigCard_Load;

    const float t = Anim(gid, selected ? 1.0f : 0.0f);
    const float hv = anim(Sub(gid, "##hov"), cardHov ? 1.0f : 0.0f, 16.0f, 0.0f);
    split.SetCurrentChannel(dl, 0);
    dl->AddRectFilled(bb.Min, bb.Max, mix(mix(C(29, 29, 35), C(34, 34, 42), hv), C(35, 33, 50), t), 8.0f);
    dl->AddRect(bb.Min, bb.Max, mix(C(255, 255, 255, 10), Accent(0.6f), t), 8.0f, 0, 1.0f);
    split.SetCurrentChannel(dl, 1);

    const ImRect tile({ bb.Min.x + 9, bb.Min.y + 9 }, { bb.Min.x + 9 + 32, bb.Min.y + 41 });
    dl->AddRectFilled(tile.Min, tile.Max, mix(C(42, 42, 52), Accent(0.22f), active ? 1.0f : t), 7.0f);
    char letter[2] = { static_cast<char>(std::toupper(static_cast<unsigned char>(name[0] ? name[0] : '?'))), 0 };
    const ImVec2 ls = TextSize(Bold(), 15.0f, letter);
    Text(dl, Bold(), 15.0f, { tile.GetCenter().x - ls.x / 2, MidY(Bold(), 15.0f, tile.GetCenter().y, letter) }, active ? Accent() : C(200, 200, 214), letter);

    float x = tile.Max.x + 11;
    if (renaming) {
        SetCursorScreenPos({ x, bb.Min.y + 9 });
        PushFont(Bold(), 13.0f);
        PushStyleVar(ImGuiStyleVar_FramePadding, { 0, 0 });
        PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        PushStyleColor(ImGuiCol_Border, ColorConvertU32ToFloat4(Accent(0.55f)));
        PushStyleColor(ImGuiCol_TextSelectedBg, ColorConvertU32ToFloat4(Accent(0.35f)));
        SetNextItemWidth(bb.Max.x - 12 - x);
        if (g_cfgRenameFocus) { SetKeyboardFocusHere(); g_cfgRenameFocus = false; }
        const bool enter = InputText("##rename", g_cfgRenameBuf, sizeof(g_cfgRenameBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool left = IsItemDeactivated();
        PopStyleColor(5);
        PopStyleVar(3);
        PopFont();
        if (enter) { result = ConfigCard_Rename; g_cfgRenaming.clear(); }
        else if (left) g_cfgRenaming.clear();
        Text(dl, Regular(), 10.5f, { x, bb.Min.y + 29 }, C(120, 120, 134), "Enter to save  \xc2\xb7  Esc to cancel");
        split.Merge(dl);
        PopID();
        w->DC.CursorPos = after;
        w->DC.CursorPosPrevLine = prevLine;
        w->DC.PrevLineSize = prevSize;
        w->DC.CurrLineSize = currSize;
        return result;
    }
    Text(dl, Bold(), 13.0f, { x, bb.Min.y + 9 }, C(240, 240, 246), name);
    float px = x + TextSize(Bold(), 13.0f, name).x + 8;
    auto pill = [&](const char* txt, ImU32 fg, ImU32 bg) {
        const ImVec2 ts = TextSize(Regular(), 10.0f, txt);
        const ImRect r({ px, bb.Min.y + 10 }, { px + ts.x + 12, bb.Min.y + 25 });
        dl->AddRectFilled(r.Min, r.Max, bg, 7.5f);
        Text(dl, Regular(), 10.0f, { r.Min.x + 6, MidY(Regular(), 10.0f, r.GetCenter().y) }, fg, txt);
        px = r.Max.x + 5;
    };
    if (active && dirty) pill("Unsaved", C(255, 190, 90), C(255, 190, 90, 34));
    else if (active) pill("Active", Accent(), Accent(0.18f));
    if (autoload) pill(ICON_FA_STAR " Startup", C(200, 200, 214), C(255, 255, 255, 16));
    Text(dl, Regular(), 10.5f, { x, bb.Min.y + 29 }, C(120, 120, 134), sub);
    split.Merge(dl);
    PopID();
    w->DC.CursorPos = after;
    w->DC.CursorPosPrevLine = prevLine;
    w->DC.PrevLineSize = prevSize;
    w->DC.CurrLineSize = currSize;
    return result;
}

bool input_row(const char* label, char* buf, size_t size, const char* hint) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems) return false;
    if (searching() && g_inPage && !Match(label)) return false;
    FlushHeaders();
    const ImVec2 p = w->DC.CursorPos;
    const ImRect bb(p, p + ImVec2(RowW(), RowH()));
    ItemSize(bb.GetSize()); g_lastMatchBottom = w->DC.CursorPos.y;
    g_firstInColumn = false;
    const ImVec2 after = w->DC.CursorPos, prevLine = w->DC.CursorPosPrevLine;
    const ImVec2 prevSize = w->DC.PrevLineSize, currSize = w->DC.CurrLineSize;

    ImDrawList* dl = w->DrawList;
    ApplyEntrance(NextEntrance());
    dl->AddRectFilled(bb.Min, bb.Max, C(29, 29, 35), kRound);
    Text(dl, Regular(), 12.5f, { bb.Min.x + 10, MidY(Regular(), 12.5f, bb.GetCenter().y) }, C(160, 160, 172), label, FindRenderedTextEnd(label));
    const float fw = bb.GetWidth() * 0.6f;
    const ImRect f({ bb.Max.x - 6 - fw, bb.Min.y + 4 }, { bb.Max.x - 6, bb.Max.y - 4 });
    dl->AddRectFilled(f.Min, f.Max, C(40, 40, 49), 4.0f);

    SetCursorScreenPos({ f.Min.x + 8, f.Min.y + 1 });
    PushID(label);
    PushFont(Regular(), 12.0f);
    PushStyleVar(ImGuiStyleVar_FramePadding, { 0, 3 });
    PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.45f, 0.45f, 0.52f, 1));
    SetNextItemWidth(f.GetWidth() - 14);
    const bool enter = InputTextWithHint("##field", hint, buf, size, ImGuiInputTextFlags_EnterReturnsTrue);
    const float ft = anim(Sub(GetItemID(), "##foc"), IsItemActive() ? 1.0f : 0.0f, 16.0f, 0.0f);
    if (ft > 0.01f) dl->AddRect(f.Min, f.Max, Accent(0.7f * ft), 4.0f);
    PopStyleColor(4);
    PopStyleVar();
    PopFont();
    PopID();

    w->DC.CursorPos = after;
    w->DC.CursorPosPrevLine = prevLine;
    w->DC.PrevLineSize = prevSize;
    w->DC.CurrLineSize = currSize;
    return enter;
}

namespace {
    enum BtnKind { Btn_Secondary = 0, Btn_Primary, Btn_Danger };
    bool KitButton(ImGuiWindow* w, ImGuiID bid, const ImRect& bb, const char* label, int kind, bool disabled) {
        if (!ItemAdd(bb, bid)) return false;
        bool hov = false, held = false;
        const bool pressed = !disabled && ButtonBehavior(bb, bid, &hov, &held);
        if (hov && !disabled) SetMouseCursor(ImGuiMouseCursor_Hand);
        const float h = anim(Sub(bid, "##hov"), hov && !disabled ? 1.0f : 0.0f, 16.0f, 0.0f);
        const float pr = anim(Sub(bid, "##prs"), held ? 1.0f : 0.0f, 26.0f, 0.0f);
        ImRect r = bb;
        r.Expand(-pr * 1.0f);
        ImDrawList* dl = w->DrawList;
        const float rnd = 7.0f;
        ImU32 bg, border, fg, ic;
        if (disabled) { bg = C(28, 28, 34); border = C(255, 255, 255, 6); fg = ic = C(92, 92, 104); }
        else if (kind == Btn_Primary) { bg = mix(Accent(0.88f), Accent(1.0f), h); border = 0; fg = ic = OnAccent(); }
        else if (kind == Btn_Danger) { bg = mix(C(255, 90, 90, 20), C(255, 90, 90, 38), h); border = C(255, 95, 95, static_cast<int>(60 + 50 * h)); fg = C(255, 150, 145); ic = C(255, 110, 100); }
        else { bg = mix(C(33, 33, 41), C(43, 43, 54), h); border = mix(C(255, 255, 255, 12), Accent(0.45f), h); fg = C(232, 232, 240); ic = Accent(); }
        dl->AddRectFilled(r.Min, r.Max, bg, rnd);
        if (border) dl->AddRect(r.Min, r.Max, border, rnd);
        const char* sep = std::strstr(label, "  ");
        const bool hasIcon = sep && (static_cast<unsigned char>(label[0]) >= 0xE0);
        const char* text = hasIcon ? sep + 2 : label;
        const float fs = 12.0f, is = 11.5f;
        const ImVec2 ts = TextSize(Regular(), fs, text);
        const ImVec2 isz = hasIcon ? TextSize(Regular(), is, label, sep) : ImVec2(0, 0);
        const float gap = hasIcon ? 8.0f : 0.0f;
        float x = std::floor(r.GetCenter().x - (isz.x + gap + ts.x) * 0.5f);
        const float cy = r.GetCenter().y;
        if (hasIcon) { Text(dl, Regular(), is, { x, std::floor(cy - isz.y * 0.5f) }, ic, label, sep); x += isz.x + gap; }
        Text(dl, Regular(), fs, { x, std::floor(cy - ts.y * 0.5f) }, fg, text);
        return pressed;
    }
}

int buttons_row(const char* id, const char* const* labels, int count, int danger, int disabledMask) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems || count <= 0) return -1;
    if (searching() && g_inPage) return -1;
    const ImVec2 p = w->DC.CursorPos;
    const float total = RowW(), gap = 6.0f, h = 30.0f;
    const float bw = (total - gap * (count - 1)) / count;
    ItemSize(ImVec2(total, h + 2));
    g_firstInColumn = false;
    int clicked = -1;
    ApplyEntrance(NextEntrance());
    PushID(id);
    for (int i = 0; i < count; ++i) {
        const ImRect bb({ std::floor(p.x + i * (bw + gap)), p.y }, { std::floor(p.x + i * (bw + gap) + bw), p.y + h });
        const int kind = i == danger ? Btn_Danger : (i == 0 && danger != 0 ? Btn_Primary : Btn_Secondary);
        if (KitButton(w, w->GetID(i), bb, labels[i], kind, (disabledMask >> i) & 1)) clicked = i;
    }
    PopID();
    return clicked;
}

int center_buttons(const char* id, const char* const* labels, int count, int danger) {
    ImGuiWindow* w = GetCurrentWindow();
    if (w->SkipItems || count <= 0) return -1;
    if (searching() && g_inPage) return -1;
    const ImVec2 p = w->DC.CursorPos;
    const float total = RowW(), gap = 8.0f, h = 30.0f;
    float widths[8] = {}, sum = 0.0f;
    count = ImMin(count, 8);
    for (int i = 0; i < count; ++i) {
        const char* sep = std::strstr(labels[i], "  ");
        widths[i] = std::floor(TextSize(Regular(), 12.0f, labels[i]).x + (sep ? 4.0f : 0.0f) + 36.0f);
        sum += widths[i] + (i ? gap : 0.0f);
    }
    ItemSize(ImVec2(total, h + 2));
    g_firstInColumn = false;
    ApplyEntrance(NextEntrance());
    int clicked = -1;
    float x = std::floor(p.x + (total - sum) * 0.5f);
    PushID(id);
    for (int i = 0; i < count; ++i) {
        const ImRect bb({ x, p.y }, { x + widths[i], p.y + h });
        if (KitButton(w, w->GetID(i), bb, labels[i], i == danger ? Btn_Danger : Btn_Secondary, false)) clicked = i;
        x += widths[i] + gap;
    }
    PopID();
    return clicked;
}

void register_panel(const char* id, const char* title, const char* subtitle, std::function<void()> body) {
    for (Panel& p : g_panels) if (p.id == id) { p.body = std::move(body); return; }
    Panel np; np.id = id; np.title = title; np.subtitle = subtitle; np.body = std::move(body);
    g_panels.push_back(std::move(np));
}

void open_panel(const char* id) {
    for (Panel& p : g_panels) {
        if (p.id != id) continue;
        if (p.open) { p.open = false; return; }
        p.open = true;
        p.openTime = GetTime();
        if (p.t <= 0.01f) { p.anchor = GetIO().MousePos; p.placed = false; p.bottomY = -1.0f; }
    }
}

void open_panel_bottom(const char* id, float x, float bottomY) {
    for (Panel& p : g_panels) {
        if (p.id != id) continue;
        if (p.open) return;
        p.open = true;
        p.openTime = GetTime();
        p.pos = { x, bottomY - p.lastH };
        p.placed = true;
        p.bottomY = bottomY;
    }
}
void close_panel(const char* id) { for (Panel& p : g_panels) if (p.id == id) p.open = false; }

float last_match_bottom() { return g_lastMatchBottom; }

bool panel_is_open(const char* id) { for (const Panel& p : g_panels) if (p.id == id) return p.open; return false; }
bool any_panel_open() { for (const Panel& p : g_panels) if (p.open) return true; return false; }

void close_panels_except(const char* keep) {
    for (Panel& p : g_panels) if (!keep || p.id != keep) p.open = false;
}

bool mouse_on_ui() {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* w = g.HoveredWindow;
    if (!w) return false;
    const char* n = w->RootWindow ? w->RootWindow->Name : w->Name;
    return std::strncmp(n, "##BodycamMod", 12) == 0 || std::strncmp(n, "##panel_", 8) == 0 || std::strncmp(n, "##cfgpop", 8) == 0 || std::strncmp(n, "##searchpop", 11) == 0;
}

static void RenderPanel(Panel& pn);

void render_panels() {
    const float dt = ImMin(GetIO().DeltaTime, 1.0f / 30.0f);
    if (IsKeyPressed(ImGuiKey_Escape, false) && !IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        Panel* newest = nullptr;
        for (Panel& p : g_panels) if (p.open && (!newest || p.openTime > newest->openTime)) newest = &p;
        if (newest) newest->open = false;
    }
    std::vector<Panel*> order;
    for (Panel& p : g_panels) {
        p.t = p.open ? ImMin(1.0f, p.t + dt / 0.26f) : ImMax(0.0f, p.t - dt / 0.17f);
        if (p.t > 0.0f) order.push_back(&p);
    }
    std::sort(order.begin(), order.end(), [](const Panel* a, const Panel* b) { return a->openTime < b->openTime; });
    for (Panel* p : order) RenderPanel(*p);
}

static void RenderPanel(Panel& pn) {
    const Panel* panel = &pn;
    const float w = 228.0f;
    float pt, psc, pdr;
    if (pn.open) {
        const float inv = pn.t - 1.0f;
        const float back = 1.0f + 2.2f * inv * inv * inv + 1.2f * inv * inv;
        pt = 1.0f - (1.0f - pn.t) * (1.0f - pn.t) * (1.0f - pn.t);
        psc = 0.94f + 0.06f * back;
        pdr = (1.0f - pt) * 10.0f;
    } else {
        const float c = 1.0f - pn.t;
        pt = 1.0f - c * c * (3.0f - 2.0f * c); pt *= pt;
        psc = 1.0f - 0.05f * c * c;
        pdr = c * c * 8.0f;
    }
    const ImVec2 ds = GetIO().DisplaySize;
    if (!pn.placed) {
        ImVec2 pos(pn.anchor.x + 18, pn.anchor.y - 30);
        if (pos.x + w > ds.x - 8) pos.x = pn.anchor.x - w - 18;
        pos.y = ImClamp(pos.y, 8.0f, ImMax(8.0f, ds.y - pn.lastH - 8.0f));
        const float gap = 10.0f;
        for (int pass = 0; pass < 12; ++pass) {
            bool moved = false;
            for (const Panel& q : g_panels) {
                if (&q == &pn || !q.open || !q.placed || q.t <= 0.0f) continue;
                const ImVec2 qa = q.pos, qb(q.pos.x + w, q.pos.y + q.lastH);
                if (pos.x >= qb.x + gap || pos.x + w <= qa.x - gap || pos.y >= qb.y + gap || pos.y + pn.lastH <= qa.y - gap) continue;
                if (qb.y + gap + pn.lastH <= ds.y - 8) { pos.x = qa.x; pos.y = qb.y + gap; }
                else if (qb.x + gap + w <= ds.x - 8) pos.x = qb.x + gap;
                else if (qa.x - gap - w >= 8) pos.x = qa.x - gap - w;
                else pos.y = qb.y + gap;
                moved = true;
            }
            if (!moved) break;
        }
        pos.y = ImClamp(pos.y, 8.0f, ImMax(8.0f, ds.y - pn.lastH - 8.0f));
        pn.pos = pos;
        pn.placed = true;
    }
    if (pn.bottomY >= 0.0f && pn.t < 1.0f) pn.pos.y = ImClamp(pn.bottomY - pn.lastH, 8.0f, ImMax(8.0f, ds.y - pn.lastH - 8.0f));
    const bool animating = pn.t < 1.0f;
    if (animating) SetNextWindowPos(pn.pos, ImGuiCond_Always);
    SetNextWindowSizeConstraints({ w, 0 }, { w, FLT_MAX });
    PushStyleVar(ImGuiStyleVar_Alpha, GetStyle().Alpha * pt);
    PushStyleVar(ImGuiStyleVar_WindowPadding, { 10, 10 });
    PushStyleVar(ImGuiStyleVar_WindowRounding, 9.0f);
    PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    PushStyleVar(ImGuiStyleVar_ItemSpacing, { 6, 2 });
    PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.088f, 0.088f, 0.108f, theme.menuAlpha));
    PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.055f));

    const std::string name = "##panel_" + panel->id;
    if (Begin(name.c_str(), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                                     (pn.open ? 0 : ImGuiWindowFlags_NoInputs))) {
        if (!IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) BringWindowToDisplayFront(GetCurrentWindow());
        if (!animating) {
            ImVec2 np = GetWindowPos();
            np.x = ImClamp(np.x, 8.0f - w * 0.5f, ds.x - w * 0.5f);
            np.y = ImClamp(np.y, 0.0f, ds.y - 30.0f);
            if (np.x != GetWindowPos().x || np.y != GetWindowPos().y) SetWindowPos(np);
            pn.pos = np;
        }
        ImDrawList* dl = GetWindowDrawList();
        ImGuiWindow* panelWin = GetCurrentWindow();
        {
            const ImVec2 wp = GetWindowPos(), ws = GetWindowSize();
            const ImVec2 cc(wp.x + ws.x * 0.5f, wp.y + ws.y * 0.5f), hs(ws.x * 0.5f * psc, ws.y * 0.5f * psc);
            ImDrawList* bg = GetBackgroundDrawList();
            for (int i = 1; i <= 6; ++i) {
                const float g = i * 2.5f;
                bg->AddRectFilled({ cc.x - hs.x - g, cc.y - hs.y - g + 4 + pdr }, { cc.x + hs.x + g, cc.y + hs.y + g + 4 + pdr },
                                  C(0, 0, 0, static_cast<int>((22 - i * 3) * pt)), 9.0f + g);
            }
        }
        if (!GetCurrentWindow()->Hidden) pn.lastH = ImMax(60.0f, GetWindowSize().y);
        const ImVec2 p = GetCursorScreenPos();
        const float availW = GetContentRegionAvail().x;
        {
            const float tw = TextSize(Bold(), 13.0f, panel->title.c_str()).x;
            const float tx = ImMin(std::floor(p.x + (availW - tw) * 0.5f), p.x + availW - 18.0f - tw);
            Text(dl, Bold(), 13.0f, { ImMax(p.x + 2, tx), p.y }, C(240, 240, 246), panel->title.c_str());
        }
        int subLines = 0;
        {
            const char* s = panel->subtitle.c_str();
            const char* end = s + panel->subtitle.size();
            const float wrapW = availW - 4.0f;
            while (s < end && subLines < 4) {
                const char* brk = Regular()->CalcWordWrapPosition(11.0f, s, end, wrapW);
                if (brk == s) brk = s + 1;
                const char* lineEnd = brk;
                while (lineEnd > s && lineEnd[-1] == ' ') --lineEnd;
                const float lw = Regular()->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, s, lineEnd).x;
                Text(dl, Regular(), 11.0f, { std::floor(p.x + (availW - lw) * 0.5f), p.y + 17 + subLines * 14.0f },
                     C(120, 120, 134), s, lineEnd);
                s = brk;
                while (s < end && (*s == ' ' || *s == '\n')) ++s;
                ++subLines;
            }
        }
        const bool close = MacClose("##x", { std::floor(p.x + availW - 6.0f) + 0.5f, std::floor(p.y + 7.0f) + 0.5f }, dl);
        SetCursorScreenPos({ p.x, p.y + 38 + ImMax(0, subLines - 1) * 14.0f });

        g_inPanel = true;
        panel->body();
        g_inPanel = false;
        const bool popupOpen = IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        const bool outside = IsMouseClicked(ImGuiMouseButton_Left) && GetTime() != pn.openTime && !mouse_on_ui();
        if (pn.open && (close || (!popupOpen && outside))) pn.open = false;
        if (psc != 1.0f || pdr != 0.0f) {
            const ImVec2 cc(panelWin->Pos.x + panelWin->Size.x * 0.5f, panelWin->Pos.y + panelWin->Size.y * 0.5f);
            for (ImDrawVert& v : dl->VtxBuffer) { v.pos.x = cc.x + (v.pos.x - cc.x) * psc; v.pos.y = cc.y + (v.pos.y - cc.y) * psc + pdr; }
            for (ImDrawCmd& cmd : dl->CmdBuffer) {
                cmd.ClipRect.x = cc.x + (cmd.ClipRect.x - cc.x) * psc; cmd.ClipRect.z = cc.x + (cmd.ClipRect.z - cc.x) * psc;
                cmd.ClipRect.y = cc.y + (cmd.ClipRect.y - cc.y) * psc + pdr; cmd.ClipRect.w = cc.y + (cmd.ClipRect.w - cc.y) * psc + pdr;
            }
        }
    }
    End();
    PopStyleColor(2);
    PopStyleVar(5);
}
}

namespace kit {
void apply_theme_mode() {
    struct Clear { ~Clear() { g_themeSkip.clear(); } } clear;
    if (theme.mode != 1) return;
    ImGuiContext& g = *GImGui;
    const ImTextureRef font = g.IO.Fonts ? g.IO.Fonts->TexRef : ImTextureRef();
    auto flip = [](ImU32 c) -> ImU32 {
        int r = (c >> IM_COL32_R_SHIFT) & 255, gr = (c >> IM_COL32_G_SHIFT) & 255, b = (c >> IM_COL32_B_SHIFT) & 255;
        const int a = (c >> IM_COL32_A_SHIFT) & 255;
        const int mx = ImMax(r, ImMax(gr, b)), mn = ImMin(r, ImMin(gr, b));
        if (mx - mn > 40) return c;
        if ((r == 255 && gr == 255 && b == 254) || (r == 21 && gr == 20 && b == 26)) return c;
        if (mx < 8 && a < 250) return c;
        const int avg = (r + gr + b) / 3;
        int d = 255 - 2 * avg;
        if (avg < 128) d -= 8;
        r = ImClamp(r + d, 0, 255); gr = ImClamp(gr + d, 0, 255); b = ImClamp(b + d, 0, 255);
        return IM_COL32(r, gr, b, a);
    };
    std::vector<uint8_t> done;
    for (ImGuiWindow* w : g.Windows) {
        if (!w->Active || w->Hidden || !w->DrawList || w->DrawList->VtxBuffer.Size == 0) continue;
        if (std::strncmp(w->Name, "##overlay", 9) == 0 || std::strncmp(w->Name, "Debug##", 7) == 0) continue;
        ImDrawList* dl = w->DrawList;
        done.assign(static_cast<size_t>(dl->VtxBuffer.Size), 0);
        for (const ThemeSkip& k : g_themeSkip)
            if (k.dl == dl) for (int i = k.v0; i < k.v1 && i < dl->VtxBuffer.Size; ++i) done[static_cast<size_t>(i)] = 1;
        for (const ImDrawCmd& cmd : dl->CmdBuffer) {
            if (cmd.UserCallback || cmd.TexRef._TexData != font._TexData || cmd.TexRef._TexID != font._TexID) continue;
            for (unsigned int i = 0; i < cmd.ElemCount; ++i) {
                const unsigned int idx = cmd.VtxOffset + dl->IdxBuffer[static_cast<int>(cmd.IdxOffset + i)];
                if (idx >= done.size() || done[idx]) continue;
                done[idx] = 1;
                dl->VtxBuffer[static_cast<int>(idx)].col = flip(dl->VtxBuffer[static_cast<int>(idx)].col);
            }
        }
    }
}
}
