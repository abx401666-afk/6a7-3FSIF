#pragma once
#include <Windows.h>
struct ImGuiIO;

void MenuInput_SetActive(bool on, HWND gameWindow);
bool MenuInput_Active();
bool MenuInput_KeyDown(int vk);
bool MenuInput_CursorPos(POINT& screen);
void MenuInput_Feed(ImGuiIO& io);
