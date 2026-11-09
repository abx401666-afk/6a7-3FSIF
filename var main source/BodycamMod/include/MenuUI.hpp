#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "menu/hashes.hpp"

template <typename T>
inline T& anim_obj(const char* id, int seed, T arg) {
    const ImGuiID im_id = ImHashStr(id, 0, seed);
    static std::unordered_map<ImGuiID, T> map;
    auto it = map.find(im_id);
    if (it == map.end()) it = map.emplace(im_id, arg).first;
    return it->second;
}

template <typename T>
inline T animate(T v, T min, T max, bool state, float speed = 14.f) {
    return ImLerp(v, state ? max : min, speed * ImGui::GetIO().DeltaTime);
}

inline ImColor col_anim(ImColor inactive, ImColor active, float anim) {
    return ImGui::ColorConvertFloat4ToU32(ImLerp(inactive.Value, active.Value, anim));
}

struct tab_t {
    const char* m_icon;
    const char* m_name;
    int cur_subtab = 0;
    int next_subtab = 0;
    std::vector<const char*> m_subtabs;
    std::vector<std::function<void()>> pages;

    tab_t(const char* icon, const char* name, std::vector<const char*> subtabs)
        : m_icon(icon), m_name(name), m_subtabs(std::move(subtabs)) {}
};

namespace ui {
    inline ImVec2 size{ 760, 520 };

    inline ImVec4 scheme{ 215.f / 255.f, 96.f / 255.f, 20.f / 255.f, 1.f };
    inline ImU32 scheme_u32(float alpha = 1.f) {
        return ImGui::GetColorU32(ImVec4(scheme.x, scheme.y, scheme.z, scheme.w * alpha));
    }

    inline float anim = 0.f;
    inline float anim_dst = 1.f;
    inline float content_anim = 0.f;
    inline float content_anim_dst = 1.f;

    inline int cur_tab = 0;
    inline int next_tab = 0;
    inline std::vector<tab_t> tabs;

    void apply_style();
    void load_fonts(ImGuiIO& io);
    ImFont* font(int index);

    bool combo(const char* label, int* current, const char* const* items, int count);
    bool button(const char* label, ImVec2 size = ImVec2(0, 0));
    bool color(const char* name, float col[4], bool alpha = true);
}
