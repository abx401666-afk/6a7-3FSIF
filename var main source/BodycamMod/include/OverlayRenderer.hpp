#pragma once

#include <Windows.h>

using OverlayRenderCallback = void (*)();

bool D3D12Overlay_Initialize(HINSTANCE instance, OverlayRenderCallback callback);
const wchar_t* D3D12Overlay_GetLastError();
bool D3D12Overlay_PumpMessages();
void D3D12Overlay_Update(HWND gameWindow, bool interactive);
void D3D12Overlay_Render();
bool D3D12Overlay_WaitForFrame();
void D3D12Overlay_Shutdown();
HWND D3D12Overlay_GetWindow();
