#include "../include/MenuInput.hpp"
#include "../include/MenuUI.hpp"
#include "../include/menu/bytes.hpp"
#include "../include/menu/inter.hpp"
#include <Windows.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

using namespace ImGui;

void ui::apply_style() {
    ImGuiStyle& s = GetStyle();
    ImVec4* c = s.Colors;

    s.WindowPadding = ImVec2(0, 0);
    s.WindowRounding = 4.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildRounding = 4.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupRounding = 4.0f;
    s.PopupBorderSize = 1.0f;
    s.FramePadding = ImVec2(12, 8);
    s.FrameRounding = 2.0f;
    s.FrameBorderSize = 0.0f;
    s.ItemSpacing = ImVec2(8, 4);
    s.ItemInnerSpacing = ImVec2(6, 9);
    s.ScrollbarSize = 2.0f;
    s.ScrollbarRounding = 9.0f;
    s.GrabMinSize = 0.0f;
    s.GrabRounding = 0.0f;

    c[ImGuiCol_Text]                = ImColor(255, 255, 255);
    c[ImGuiCol_TextDisabled]        = ImColor(100, 100, 100);
    c[ImGuiCol_WindowBg]            = ImColor(16, 16, 16);
    c[ImGuiCol_ChildBg]             = ImColor(20, 20, 20);
    c[ImGuiCol_PopupBg]             = ImColor(31, 31, 31);
    c[ImGuiCol_Border]              = ImColor(255, 255, 255, 15);
    c[ImGuiCol_BorderShadow]        = ImColor(255, 255, 255, 25);
    c[ImGuiCol_FrameBg]             = ImColor(16, 16, 16);
    c[ImGuiCol_FrameBgHovered]      = ImColor(21, 21, 21);
    c[ImGuiCol_FrameBgActive]       = ImColor(26, 26, 26);
    c[ImGuiCol_ScrollbarBg]         = ImColor(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]       = ImColor(28, 28, 28);
    c[ImGuiCol_ScrollbarGrabHovered]= ImColor(33, 33, 33);
    c[ImGuiCol_ScrollbarGrabActive] = ImColor(38, 38, 38);
    c[ImGuiCol_Button]              = ImColor(26, 26, 26);
    c[ImGuiCol_ButtonHovered]       = ImColor(31, 31, 31);
    c[ImGuiCol_ButtonActive]        = ImColor(36, 36, 36);
    c[ImGuiCol_Header]              = ImColor(26, 26, 26);
    c[ImGuiCol_HeaderHovered]       = ImColor(31, 31, 31);
    c[ImGuiCol_HeaderActive]        = ImColor(36, 36, 36);
    c[ImGuiCol_Separator]           = c[ImGuiCol_Border];
}

static void* ReadFontFile(const wchar_t* const* names, int& size) {
    const wchar_t* roots[3] = { _wgetenv(L"APPDATA"), _wgetenv(L"LOCALAPPDATA"), _wgetenv(L"WINDIR") };
    const wchar_t* subs[3]  = { L"\\BodycamMod\\fonts\\", L"\\Microsoft\\Windows\\Fonts\\", L"\\Fonts\\" };
    for (const wchar_t* const* n = names; *n; ++n) {
        for (int r = 0; r < 3; ++r) {
            if (!roots[r] || !*roots[r]) continue;
            wchar_t path[MAX_PATH];
            _snwprintf(path, MAX_PATH, L"%s%s%s", roots[r], subs[r], *n);
            path[MAX_PATH - 1] = 0;
            FILE* f = _wfopen(path, L"rb");
            if (!f) continue;
            std::fseek(f, 0, SEEK_END);
            const long len = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            void* data = len > 1024 ? IM_ALLOC(static_cast<size_t>(len)) : nullptr;
            const bool ok = data && std::fread(data, 1, static_cast<size_t>(len), f) == static_cast<size_t>(len);
            std::fclose(f);
            if (ok) { size = static_cast<int>(len); return data; }
            if (data) IM_FREE(data);
        }
    }
    return nullptr;
}

void ui::load_fonts(ImGuiIO& io) {
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;

    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.FontDataOwnedByAtlas = false;

    static const wchar_t* const kSfRegular[] = { L"SF-Pro-Text-Medium.otf", L"SF-Pro-Display-Medium.otf", L"SF-Pro-Text-Regular.otf",
                                                 L"SF-Pro-Display-Regular.otf", L"SF-Pro.ttf", L"SFProText-Medium.ttf", L"SFProDisplay-Medium.ttf",
                                                 L"SFProText-Regular.ttf", L"SFProDisplay-Regular.ttf", nullptr };
    static const wchar_t* const kSfBold[]    = { L"SF-Pro-Text-Semibold.otf", L"SF-Pro-Display-Semibold.otf", L"SF-Pro-Text-Bold.otf",
                                                 L"SF-Pro-Display-Bold.otf", L"SFProText-Semibold.ttf", L"SFProDisplay-Semibold.ttf",
                                                 L"SFProText-Bold.ttf", L"SFProDisplay-Bold.ttf", nullptr };
    int regSize = 0, boldSize = 0;
    void* reg = ReadFontFile(kSfRegular, regSize);
    void* bold = reg ? ReadFontFile(kSfBold, boldSize) : nullptr;
    ImFontConfig owned;
    owned.FontDataOwnedByAtlas = true;
    auto addReg = [&](float px, const ImFontConfig* fallbackCfg) {
        if (reg) { void* copy = IM_ALLOC(regSize); std::memcpy(copy, reg, regSize); return io.Fonts->AddFontFromMemoryTTF(copy, regSize, px, &owned); }
        return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(inter_medium_bin), sizeof(inter_medium_bin), px, fallbackCfg);
    };
    auto addBold = [&](float px, const ImFontConfig* fallbackCfg) {
        void* src = bold ? bold : reg;
        const int n = bold ? boldSize : regSize;
        if (src) { void* copy = IM_ALLOC(n); std::memcpy(copy, src, n); return io.Fonts->AddFontFromMemoryTTF(copy, n, px, &owned); }
        return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(inter_semibold_bin), sizeof(inter_semibold_bin), px, fallbackCfg);
    };
    addReg(14, &config);
    io.Fonts->AddFontFromMemoryTTF(font_awesome_bin, sizeof(font_awesome_bin), 13, &icons_config, icon_ranges);
    addBold(14, &config);
    addReg(17, &config);
    addBold(17, &config);
    if (reg) IM_FREE(reg);
    if (bold) IM_FREE(bold);
}

ImFont* ui::font(int index) {
    ImFontAtlas* atlas = GetIO().Fonts;
    if (index >= 0 && index < atlas->Fonts.Size) return atlas->Fonts[index];
    return GetFont();
}

bool ui::button(const char* label, ImVec2 size_arg) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, nullptr, true);
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 sz = CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);
    const ImRect bb(pos, pos + sz);
    ItemSize(sz, style.FramePadding.y);
    if (!ItemAdd(bb, id)) return false;

    bool hovered, held;
    const bool pressed = ButtonBehavior(bb, id, &hovered, &held);

    struct S { float held; float hover; ImColor col; };
    S& obj = anim_obj(label, 3, S{ 0.f, 0.f, ImColor(0) });
    obj.held = animate(obj.held, 0.f, 1.f, held);
    obj.hover = animate(obj.hover, 0.f, 1.f, hovered);
    obj.col = col_anim(col_anim(GetColorU32(ImGuiCol_Button), GetColorU32(ImGuiCol_ButtonHovered), obj.hover),
                       GetColorU32(ImGuiCol_ButtonActive), obj.held);

    RenderFrame(bb.Min, bb.Max, obj.col, false, style.FrameRounding);
    window->DrawList->AddText(bb.GetCenter() - label_size / 2, GetColorU32(ImGuiCol_Text), label, FindRenderedTextEnd(label));
    return pressed;
}

bool ui::combo(const char* label, int* current, const char* const* items, int count) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems || count <= 0) return false;
    const ImGuiStyle& style = GetStyle();
    const ImGuiID id = window->GetID(label);
    *current = ImClamp(*current, 0, count - 1);

    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 label_size = CalcTextSize(label, nullptr, true);
    const ImRect bb(pos + ImVec2(0, label_size.y + style.ItemInnerSpacing.y),
                    pos + ImVec2(CalcItemWidth(), label_size.y + style.ItemInnerSpacing.y + 32));
    const ImRect total_bb(pos, bb.Max);
    ItemSize(total_bb);
    if (!ItemAdd(total_bb, id)) return false;

    bool hovered, held;
    const bool pressed = ButtonBehavior(bb, id, &hovered, &held);
    const ImGuiID popup_id = ImHashStr("##ComboPopup", 0, id);
    bool open = IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    if (pressed && !open) { OpenPopupEx(popup_id, ImGuiPopupFlags_None); open = true; }

    struct S { float anim; float rad; ImColor col; ImColor arrow; };
    S& obj = anim_obj(label, 5, S{ 0.f, IM_PI / 2, ImColor(0), ImColor(0) });
    obj.anim = animate(obj.anim, 0.f, 1.f, hovered || open);
    obj.rad = animate(obj.rad, IM_PI / 2, IM_PI * 1.5f, open);
    obj.col = col_anim(GetColorU32(ImGuiCol_Button), GetColorU32(ImGuiCol_ButtonHovered), obj.anim);
    obj.arrow = col_anim(GetColorU32(ImGuiCol_TextDisabled), GetColorU32(ImGuiCol_Text), obj.anim);

    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(bb.Min, bb.Max, obj.col, style.FrameRounding, open ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
    const float arrowScale = 0.8f;
    RenderArrow(dl, { bb.Max.x - 18 - GetFontSize() * 0.5f, bb.GetCenter().y - GetFontSize() * 0.5f * arrowScale },
                obj.arrow, open ? ImGuiDir_Up : ImGuiDir_Down, arrowScale);
    dl->AddText(total_bb.Min, GetColorU32(ImGuiCol_TextDisabled), label, FindRenderedTextEnd(label));
    dl->PushClipRect(bb.Min, bb.Max - ImVec2(35, 0), true);
    dl->AddText(bb.Min + style.FramePadding, GetColorU32(ImGuiCol_Text), items[*current]);
    dl->PopClipRect();

    bool changed = false;
    if (open) {
        const float row = 28.f;
        SetNextWindowPos({ bb.Min.x, bb.Max.y + 1 });
        SetNextWindowSize({ bb.GetWidth(), row * count + 10 });
        PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0);
        PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 5 });
        PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
        PushStyleColor(ImGuiCol_Header, scheme_u32(0.18f));
        PushStyleColor(ImGuiCol_HeaderHovered, scheme_u32(0.30f));
        PushStyleColor(ImGuiCol_HeaderActive, scheme_u32(0.40f));
        const bool begun = BeginPopupEx(popup_id, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        PopStyleVar(3);
        if (begun) {
            for (int i = 0; i < count; ++i) {
                const bool sel = i == *current;
                if (Selectable((std::string("##") + items[i]).c_str(), sel, 0, { bb.GetWidth(), row })) {
                    *current = i; changed = true; CloseCurrentPopup();
                }
                const ImVec2 mn = GetItemRectMin();
                GetWindowDrawList()->AddText({ mn.x + 12, mn.y + (row - GetFontSize()) / 2 },
                                             sel ? scheme_u32() : GetColorU32(ImGuiCol_Text), items[i]);
            }
            EndPopup();
        }
        PopStyleColor(3);
    }
    return changed;
}

const char* VkName(int vk, char* buf, size_t n) {
    switch (vk) {
    case 0: return "NONE";
    case VK_LBUTTON: return "MOUSE 1";
    case VK_RBUTTON: return "MOUSE 2";
    case VK_MBUTTON: return "MOUSE 3";
    case VK_XBUTTON1: return "MOUSE 4";
    case VK_XBUTTON2: return "MOUSE 5";
    case VK_LSHIFT: case VK_SHIFT: return "SHIFT";
    case VK_LCONTROL: case VK_CONTROL: return "CTRL";
    case VK_LMENU: case VK_MENU: return "ALT";
    case VK_SPACE: return "SPACE";
    case VK_CAPITAL: return "CAPS";
    }
    LONG scan = static_cast<LONG>(MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC)) << 16;
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT ||
        vk == VK_END || vk == VK_HOME || vk == VK_INSERT || vk == VK_DELETE) scan |= 1 << 24;
    if (GetKeyNameTextA(scan, buf, static_cast<int>(n)) > 0) return buf;
    std::snprintf(buf, n, "KEY %d", vk);
    return buf;
}

static const ImGuiColorEditFlags kAlphaFlags =
    ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs |
    ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_InputRGB |
    ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar |
    ImGuiColorEditFlags_NoBorder;
static const ImGuiColorEditFlags kNoAlphaFlags =
    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
    ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float |
    ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder;

bool ui::color(const char* name, float col[4], bool alpha) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID id = window->GetID(name);
    ImDrawList* draw = window->DrawList;
    ImVec2 pos = window->DC.CursorPos;

    const ImVec2 sq{ 16, 16 };
    const ImVec2 label_size = CalcTextSize(name, nullptr, true);
    const ImRect bb(pos, { pos.x + CalcItemWidth() - sq.x, pos.y + sq.y });
    ItemSize(bb);
    ItemAdd(bb, id);
    draw->AddText({ bb.Min.x, bb.GetCenter().y - label_size.y / 2 }, GetColorU32(ImGuiCol_Text), name, FindRenderedTextEnd(name));

    bool changed = false;
    if (col) {
        pos = window->DC.CursorPos;
        window->DC.CursorPos = ImVec2{ bb.Max.x, bb.Min.y };
        changed = ColorEdit4((std::string(name) + "##col").c_str(), col, alpha ? kAlphaFlags : kNoAlphaFlags);
        window->DC.CursorPos = pos;
    }
    return changed;
}

