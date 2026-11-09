#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>

float WeaponIcon_Width(int kind, float h, const char* name = nullptr);
void  WeaponIcon_Draw(ImDrawList* dl, int kind, ImVec2 pos, float h, ImU32 col, bool shadow = false, const char* name = nullptr);
