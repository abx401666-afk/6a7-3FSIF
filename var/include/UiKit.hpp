#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <functional>
#include <string>
#include "menu/hashes.hpp"

namespace kit {
    struct Theme {
        ImVec4 accent    = { 0.482f, 0.424f, 0.965f, 1.0f };
        float  menuAlpha = 0.97f;
        float  hudAlpha  = 0.92f;
        bool   notifyOnToggle = true;
        int    mode = 0;
    };
    extern Theme theme;

    ImU32 C(int r, int g, int b, int a = 255);
    void apply_theme_mode();
    ImU32 Accent(float alpha = 1.0f);
    ImU32 OnAccent(float alpha = 1.0f);
    bool  AccentIsLight();
    void Capsule(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col);
    bool MacClose(const char* id, ImVec2 c, ImDrawList* dl = nullptr);
    ImFont* Regular();
    ImFont* Bold();

    void   Text(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const char* s, const char* e = nullptr);
    ImVec2 TextSize(ImFont* f, float size, const char* s, const char* e = nullptr);
    ImVec4 TextInk(ImFont* f, float size, const char* s, const char* e = nullptr);
    float MidY(ImFont* f, float size, float cy, const char* ink = nullptr);
    void TextCentered(ImDrawList* dl, ImFont* f, float size, ImVec2 c, ImU32 col, const char* s, const char* e = nullptr);

    void begin_page(float width);
    void column(int index);
    void end_page();
    void section(const char* title);
    void spacing(float px = 6.0f);

    void set_filter(const char* text);
    bool searching();
    void set_search_context(const char* path);
    bool page_had_matches();
    int  search_match_count();

    bool toggle(const char* label, bool* v, const char* settings = nullptr);
    bool slider(const char* label, float* v, float mn, float mx, const char* fmt, const char* settings = nullptr);
    bool segmented(const char* label, int* v, const char* const* items, int count);
    bool combo(const char* label, int* cur, const char* const* items, int count);
    bool multi(const char* label, unsigned* mask, const char* const* items, int count);
    bool keybind(const char* label, int* vk);
    bool color(const char* label, ImVec4* c);
    bool toggle_color(const char* label, bool* v, ImVec4* c, const char* settings = nullptr);
    void value_row(const char* label, const char* value, ImU32 valueCol = 0);
    bool button_row(const char* label, const char* button);
    void note(const char* text);
    void compact_rows(bool on);
    void set_disabled(bool on);
    enum { ConfigCard_None = 0, ConfigCard_Select, ConfigCard_Load, ConfigCard_Save, ConfigCard_Delete, ConfigCard_Rename };
    const char* config_card_rename_text();
    int  config_card(const char* name, const char* sub, bool selected, bool active, bool dirty, bool autoload, bool deleteArmed);
    bool input_row(const char* label, char* buf, size_t size, const char* hint);
    int  buttons_row(const char* id, const char* const* labels, int count, int danger = -1, int disabledMask = 0);
    int  center_buttons(const char* id, const char* const* labels, int count, int danger = -1);

    void register_panel(const char* id, const char* title, const char* subtitle, std::function<void()> body);
    void open_panel(const char* id);
    void open_panel_bottom(const char* id, float x, float bottomY);
    void close_panel(const char* id);
    void close_panels_except(const char* keep);
    void tip(ImVec2 anchor, const char* icon, const char* title, const char* line, float t);
    bool panel_is_open(const char* id);
    float last_match_bottom();
bool any_panel_open();
bool mouse_on_ui();
    void render_panels();

    extern std::function<void(const char*, bool)> on_toggle;

    void Card(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float alpha = 1.0f);
    void Toggle(ImDrawList* dl, ImVec2 pos, float t);
    const char* KeyName(int vk);

    float anim(ImGuiID id, float target, float speed = 14.0f, float init = -1.0f);
    ImU32 mix(ImU32 a, ImU32 b, float t);
    float ease_out(float t);
    void  restart_entrance();
}
