#include "../include/MenuInput.hpp"
#include "../include/OverlayRenderer.hpp"
#include "../include/Memory.hpp"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <array>
#include <cstdint>
#include <vector>

void Menu_Setup();

using Microsoft::WRL::ComPtr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
    constexpr UINT kFrameCount = 2;
    constexpr wchar_t kOverlayClass[] = L"BodycamExternalOverlayWindow";

    HINSTANCE g_instance = nullptr;
    HWND g_overlay = nullptr;
    OverlayRenderCallback g_renderCallback = nullptr;
    bool g_interactive = false;
    bool g_focusGamePending = false;
    ULONGLONG g_interactiveSince = 0;
    bool g_interactivityInitialized = false;
    bool g_imguiReady = false;
    bool g_imguiContextCreated = false;
    bool g_win32BackendReady = false;
    bool g_dx12BackendReady = false;
    bool g_quit = false;
    wchar_t g_initError[256] = L"No initialization error details available.";
    UINT g_width = 0;
    UINT g_height = 0;
    UINT64 g_nextFenceValue = 1;
    std::array<UINT64, kFrameCount> g_frameFenceValues{};

    ComPtr<IDXGIFactory4> g_factory;
    ComPtr<ID3D12Device> g_device;
    ComPtr<ID3D12CommandQueue> g_queue;
    ComPtr<IDXGISwapChain3> g_swapChain;
    ComPtr<IDCompositionDevice> g_compositionDevice;
    ComPtr<IDCompositionTarget> g_compositionTarget;
    ComPtr<IDCompositionVisual> g_compositionVisual;
    ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> g_srvHeap;
    ComPtr<ID3D12GraphicsCommandList> g_commandList;
    ComPtr<ID3D12Fence> g_fence;
    std::array<ComPtr<ID3D12CommandAllocator>, kFrameCount> g_allocators{};
    std::array<ComPtr<ID3D12Resource>, kFrameCount> g_renderTargets{};
    HANDLE g_fenceEvent = nullptr;
    HANDLE g_frameLatencyWait = nullptr;

    LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (!g_interactive) {
            if (message == WM_NCHITTEST) return HTTRANSPARENT;
            if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        }

        if (g_interactive && g_imguiReady &&
            ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
            return 1;

        switch (message) {
        case WM_CLOSE:
            g_quit = true;
            return 0;
        case WM_DESTROY:
            g_quit = true;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    bool CheckHr(HRESULT hr, const wchar_t* stage) {
        if (SUCCEEDED(hr)) return true;
        swprintf_s(g_initError, L"%s failed (HRESULT 0x%08X).", stage,
            static_cast<unsigned int>(hr));
        return false;
    }

    bool SetInitError(const wchar_t* message) {
        wcscpy_s(g_initError, message);
        return false;
    }

    bool WaitForGpu() {
        if (!g_queue || !g_fence || !g_fenceEvent) return false;
        const UINT64 value = g_nextFenceValue++;
        if (FAILED(g_queue->Signal(g_fence.Get(), value))) return false;
        if (g_fence->GetCompletedValue() < value) {
            if (FAILED(g_fence->SetEventOnCompletion(value, g_fenceEvent))) return false;
            if (WaitForSingleObject(g_fenceEvent, 5000) != WAIT_OBJECT_0) return false;
        }
        return true;
    }

    void ReleaseRenderTargets() {
        for (auto& target : g_renderTargets) target.Reset();
    }

    bool CreateRenderTargets() {
        if (!g_swapChain || !g_device || !g_rtvHeap) return false;
        const UINT descriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE handle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < kFrameCount; ++i) {
            if (FAILED(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])))) return false;
            g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr, handle);
            handle.ptr += descriptorSize;
        }
        return true;
    }

    bool ResizeSwapChain(UINT width, UINT height) {
        if (!g_swapChain || width == 0 || height == 0) return false;
        if (width == g_width && height == g_height) return true;
        if (!WaitForGpu()) return false;
        ReleaseRenderTargets();
        if (!CheckHr(g_swapChain->ResizeBuffers(kFrameCount, width, height,
                DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT), L"IDXGISwapChain::ResizeBuffers")) return false;
        g_width = width;
        g_height = height;
        return CreateRenderTargets();
    }

    void ReleaseD3D() {
        if (g_dx12BackendReady) {
            ImGui_ImplDX12_Shutdown();
            g_dx12BackendReady = false;
        }
        if (g_win32BackendReady) {
            ImGui_ImplWin32_Shutdown();
            g_win32BackendReady = false;
        }
        if (g_imguiContextCreated) {
            ImGui::DestroyContext();
            g_imguiContextCreated = false;
        }
        g_imguiReady = false;
        WaitForGpu();
        ReleaseRenderTargets();
        for (auto& allocator : g_allocators) allocator.Reset();
        g_commandList.Reset();
        g_srvHeap.Reset();
        g_rtvHeap.Reset();
        g_compositionVisual.Reset();
        g_compositionTarget.Reset();
        g_compositionDevice.Reset();
        g_swapChain.Reset();
        g_fence.Reset();
        g_queue.Reset();
        g_device.Reset();
        g_factory.Reset();
        if (g_fenceEvent) {
            CloseHandle(g_fenceEvent);
            g_fenceEvent = nullptr;
        }
    }

    bool CreateD3D() {
        if (!CheckHr(CreateDXGIFactory1(IID_PPV_ARGS(&g_factory)), L"CreateDXGIFactory1"))
            return false;

        if (!CheckHr(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&g_device)), L"D3D12CreateDevice"))
            return false;

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
        if (FAILED(g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_queue)))) {
            queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            if (!CheckHr(g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_queue)),
                    L"ID3D12Device::CreateCommandQueue"))
                return false;
        }

        DXGI_SWAP_CHAIN_DESC1 swapDesc{};
        swapDesc.Width = g_width;
        swapDesc.Height = g_height;
        swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapDesc.Stereo = FALSE;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.SampleDesc.Quality = 0;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = kFrameCount;
        swapDesc.Scaling = DXGI_SCALING_STRETCH;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swapDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        swapDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

        ComPtr<IDXGISwapChain1> swapChain1;
        if (!CheckHr(g_factory->CreateSwapChainForComposition(g_queue.Get(),
                &swapDesc, nullptr, &swapChain1), L"IDXGIFactory2::CreateSwapChainForComposition"))
            return false;
        if (!CheckHr(swapChain1.As(&g_swapChain), L"Query IDXGISwapChain3"))
            return false;
        g_swapChain->SetMaximumFrameLatency(1);
        g_frameLatencyWait = g_swapChain->GetFrameLatencyWaitableObject();

        if (!CheckHr(DCompositionCreateDevice2(nullptr, IID_PPV_ARGS(&g_compositionDevice)),
                L"DCompositionCreateDevice2"))
            return false;
        if (!CheckHr(g_compositionDevice->CreateTargetForHwnd(g_overlay, TRUE,
                &g_compositionTarget), L"IDCompositionDevice::CreateTargetForHwnd"))
            return false;
        if (!CheckHr(g_compositionDevice->CreateVisual(&g_compositionVisual),
                L"IDCompositionDevice::CreateVisual"))
            return false;
        if (!CheckHr(g_compositionVisual->SetContent(g_swapChain.Get()),
                L"IDCompositionVisual::SetContent"))
            return false;
        if (!CheckHr(g_compositionTarget->SetRoot(g_compositionVisual.Get()),
                L"IDCompositionTarget::SetRoot"))
            return false;
        if (!CheckHr(g_compositionDevice->Commit(), L"IDCompositionDevice::Commit"))
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = kFrameCount;
        if (!CheckHr(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)),
                L"Create RTV descriptor heap"))
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = 1;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!CheckHr(g_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap)),
                L"Create shader-visible SRV descriptor heap"))
            return false;

        for (auto& allocator : g_allocators) {
            if (!CheckHr(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&allocator)), L"Create D3D12 command allocator"))
                return false;
        }
        if (!CheckHr(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                g_allocators[0].Get(), nullptr, IID_PPV_ARGS(&g_commandList)),
                L"Create D3D12 command list"))
            return false;
        if (!CheckHr(g_commandList->Close(), L"Close initial D3D12 command list"))
            return false;

        if (!CheckHr(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&g_fence)), L"Create D3D12 fence"))
            return false;
        g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_fenceEvent) {
            const DWORD error = GetLastError();
            swprintf_s(g_initError, L"CreateEventW failed (Win32 error %lu).",
                static_cast<unsigned long>(error));
            return false;
        }

        if (!CreateRenderTargets())
            return SetInitError(L"Could not acquire the swap-chain back buffers or create render-target views.");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        g_imguiContextCreated = true;
        Menu_Setup();

        if (!ImGui_ImplWin32_Init(g_overlay))
            return SetInitError(L"ImGui_ImplWin32_Init failed.");
        g_win32BackendReady = true;

        ImGui_ImplDX12_InitInfo dx12Info{};
        dx12Info.Device = g_device.Get();
        dx12Info.CommandQueue = g_queue.Get();
        dx12Info.NumFramesInFlight = static_cast<int>(kFrameCount);
        dx12Info.RTVFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        dx12Info.DSVFormat = DXGI_FORMAT_UNKNOWN;
        dx12Info.SrvDescriptorHeap = g_srvHeap.Get();
        dx12Info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
                D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
            *cpu = info->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            *gpu = info->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        };
        dx12Info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*,
                D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};
        if (!ImGui_ImplDX12_Init(&dx12Info))
            return SetInitError(L"ImGui_ImplDX12_Init failed.");
        g_dx12BackendReady = true;
        g_imguiReady = true;
        return true;
    }

    void FocusWindow(HWND window) {
        if (!window || !IsWindow(window)) return;
        if (GetForegroundWindow() == window) { SetFocus(window); return; }

        const HWND fg = GetForegroundWindow();
        const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
        const DWORD currentThread = GetCurrentThreadId();
        const bool attached = fgThread != 0 && fgThread != currentThread &&
            AttachThreadInput(currentThread, fgThread, TRUE) != FALSE;

        BringWindowToTop(window);
        bool ok = SetForegroundWindow(window) != FALSE;
        if (!ok) {
            INPUT alt[2]{};
            alt[0].type = INPUT_KEYBOARD; alt[0].ki.wVk = VK_MENU;
            alt[1].type = INPUT_KEYBOARD; alt[1].ki.wVk = VK_MENU; alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, alt, sizeof(INPUT));
            ok = SetForegroundWindow(window) != FALSE;
        }
        SetActiveWindow(window);
        SetFocus(window);
        if (attached) AttachThreadInput(currentThread, fgThread, FALSE);
    }

    void ApplyInteractivity(bool interactive) {
        if (!g_overlay || (g_interactivityInitialized && g_interactive == interactive)) return;

        const bool wasInteractive = g_interactivityInitialized && g_interactive;
        g_interactive = interactive;
        g_interactivityInitialized = true;

        static bool hookOpen = false;
        if (interactive && MenuInput_Active()) { hookOpen = true; g_focusGamePending = false; return; }
        if (!interactive && hookOpen) {
            hookOpen = false;
            if (g_imguiContextCreated) { ImGuiIO& io = ImGui::GetIO(); for (int b = 0; b < 5; ++b) io.AddMouseButtonEvent(b, false); }
            return;
        }

        LONG_PTR style = GetWindowLongPtrW(g_overlay, GWL_EXSTYLE);
        if (interactive) {
            style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        } else {
            style |= static_cast<LONG_PTR>(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        }
        SetWindowLongPtrW(g_overlay, GWL_EXSTYLE, style);

        if (interactive) {
            g_focusGamePending = false;
            g_interactiveSince = GetTickCount64();
            FocusWindow(g_overlay);
            SetCapture(g_overlay);
            RECT overlayRect{};
            if (GetWindowRect(g_overlay, &overlayRect)) {
                const LONG overlayWidth = overlayRect.right - overlayRect.left;
                const LONG overlayHeight = overlayRect.bottom - overlayRect.top;
                const LONG menuWidth = (std::min)(800L, overlayWidth);
                const LONG menuHeight = (std::min)(600L, overlayHeight);
                RECT menuRect{
                    overlayRect.left + (overlayWidth - menuWidth) / 2,
                    overlayRect.top + (overlayHeight - menuHeight) / 2,
                    overlayRect.left + (overlayWidth + menuWidth) / 2,
                    overlayRect.top + (overlayHeight + menuHeight) / 2
                };
                SetCursorPos((menuRect.left + menuRect.right) / 2,
                             (menuRect.top + menuRect.bottom) / 2);
                ClipCursor(&overlayRect);
            }
        } else {
            ClipCursor(nullptr);
            if (GetCapture() == g_overlay) ReleaseCapture();
            g_focusGamePending = wasInteractive;

            if (g_imguiContextCreated) {
                ImGuiIO& io = ImGui::GetIO();
                for (int button = 0; button < 5; ++button)
                    io.AddMouseButtonEvent(button, false);
            }
        }
    }
}

bool D3D12Overlay_Initialize(HINSTANCE instance, OverlayRenderCallback callback) {
    g_instance = instance;
    g_renderCallback = callback;
    g_quit = false;
    g_interactive = false;
    g_interactivityInitialized = false;
    wcscpy_s(g_initError, L"Overlay initialization did not complete.");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = g_instance;
    wc.lpszClassName = kOverlayClass;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        const DWORD error = GetLastError();
        swprintf_s(g_initError, L"RegisterClassExW failed (Win32 error %lu).", static_cast<unsigned long>(error));
        return false;
    }

    g_overlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        kOverlayClass, L"Bodycam External Overlay", WS_POPUP,
        0, 0, 800, 600, nullptr, nullptr, g_instance, nullptr);
    if (!g_overlay) {
        const DWORD error = GetLastError();
        swprintf_s(g_initError, L"CreateWindowExW for overlay failed (Win32 error %lu).", static_cast<unsigned long>(error));
        return false;
    }

    if (!SetLayeredWindowAttributes(g_overlay, 0, 255, LWA_ALPHA)) {
        const DWORD error = GetLastError();
        swprintf_s(g_initError, L"SetLayeredWindowAttributes failed (Win32 error %lu).", static_cast<unsigned long>(error));
        DestroyWindow(g_overlay);
        g_overlay = nullptr;
        UnregisterClassW(kOverlayClass, g_instance);
        return false;
    }

    UpdateWindow(g_overlay);

    RECT initial{ 0, 0, 800, 600 };
    g_width = static_cast<UINT>(initial.right - initial.left);
    g_height = static_cast<UINT>(initial.bottom - initial.top);
    if (!CreateD3D()) {
        ReleaseD3D();
        DestroyWindow(g_overlay);
        g_overlay = nullptr;
        UnregisterClassW(kOverlayClass, g_instance);
        return false;
    }

    return true;
}

const wchar_t* D3D12Overlay_GetLastError() { return g_initError; }

bool D3D12Overlay_PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) g_quit = true;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return !g_quit;
}

void D3D12Overlay_Update(HWND gameWindow, bool interactive) {
    if (!g_overlay) return;

    ApplyInteractivity(interactive);

    if (!gameWindow || !IsWindow(gameWindow) || IsIconic(gameWindow)) {
        if (GetCapture() == g_overlay) ReleaseCapture();
        ClipCursor(nullptr);
        ShowWindow(g_overlay, SW_HIDE);
        return;
    }

    HWND foreground = GetForegroundWindow();
    DWORD foregroundPid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &foregroundPid);
    DWORD gamePid = 0;
    GetWindowThreadProcessId(gameWindow, &gamePid);
    static ULONGLONG otherSince = 0;
    static bool shown = false;
    const bool ours = foreground == g_overlay || (gamePid && foregroundPid == gamePid);
    if (ours) otherSince = 0;
    else if (!otherSince) otherSince = GetTickCount64();
    if (!ours && shown && GetTickCount64() - otherSince < (foreground == nullptr ? 1000u : 200u)) {
    } else if (!ours) {
        if (GetCapture() == g_overlay) ReleaseCapture();
        ClipCursor(nullptr);
        ShowWindow(g_overlay, SW_HIDE);
        shown = false;
        return;
    }
    shown = true;

    RECT client{};
    if (!GetClientRect(gameWindow, &client)) return;
    POINT topLeft{ client.left, client.top };
    POINT bottomRight{ client.right, client.bottom };
    ClientToScreen(gameWindow, &topLeft);
    ClientToScreen(gameWindow, &bottomRight);
    const UINT width = static_cast<UINT>(bottomRight.x - topLeft.x);
    const UINT height = static_cast<UINT>(bottomRight.y - topLeft.y);
    if (width < 2 || height < 2) return;

    extern float g_MenuAnimT;
    if (g_focusGamePending && !g_interactive && g_MenuAnimT <= 0.0f) {
        g_focusGamePending = false;
        if (GetForegroundWindow() == g_overlay) FocusWindow(gameWindow);
    }

    SetWindowPos(g_overlay, HWND_TOPMOST, topLeft.x, topLeft.y,
        static_cast<int>(width), static_cast<int>(height), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ResizeSwapChain(width, height);

    static ULONGLONG lastFocusTry = 0;
    const ULONGLONG nowMs = GetTickCount64();
    if (g_interactive && !MenuInput_Active() && foreground != g_overlay && nowMs - g_interactiveSince > 320 && nowMs - lastFocusTry > 250) {
        lastFocusTry = GetTickCount64();
        FocusWindow(g_overlay);
        SetCapture(g_overlay);
    }
}

void D3D12Overlay_Render() {
    if (!g_imguiReady || !g_swapChain || !g_commandList || !g_queue) return;

    const UINT frameIndex = g_swapChain->GetCurrentBackBufferIndex();
    if (frameIndex >= kFrameCount) return;
    if (g_frameFenceValues[frameIndex] != 0 &&
        g_fence->GetCompletedValue() < g_frameFenceValues[frameIndex]) {
        if (FAILED(g_fence->SetEventOnCompletion(g_frameFenceValues[frameIndex], g_fenceEvent))) return;
        if (WaitForSingleObject(g_fenceEvent, 2000) != WAIT_OBJECT_0) return;
    }

    if (FAILED(g_allocators[frameIndex]->Reset())) return;
    if (FAILED(g_commandList->Reset(g_allocators[frameIndex].Get(), nullptr))) return;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = g_renderTargets[frameIndex].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_commandList->ResourceBarrier(1, &barrier);

    const UINT descriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameIndex) * descriptorSize;
    g_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get() };
    g_commandList->SetDescriptorHeaps(1, heaps);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (g_renderCallback) g_renderCallback();
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_commandList.Get());

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_commandList->ResourceBarrier(1, &barrier);
    if (FAILED(g_commandList->Close())) return;

    ID3D12CommandList* commandLists[] = { g_commandList.Get() };
    g_queue->ExecuteCommandLists(1, commandLists);
    const HRESULT presentResult = g_swapChain->Present(0, 0);
    if (FAILED(presentResult) && presentResult != DXGI_STATUS_OCCLUDED) return;

    const UINT64 fenceValue = g_nextFenceValue++;
    if (SUCCEEDED(g_queue->Signal(g_fence.Get(), fenceValue)))
        g_frameFenceValues[frameIndex] = fenceValue;
}

bool D3D12Overlay_WaitForFrame() {
    if (!g_frameLatencyWait) return false;
    return WaitForSingleObjectEx(g_frameLatencyWait, 100, TRUE) == WAIT_OBJECT_0;
}

void D3D12Overlay_Shutdown() {
    ReleaseD3D();
    if (g_overlay) {
        DestroyWindow(g_overlay);
        g_overlay = nullptr;
    }
    if (g_instance) UnregisterClassW(kOverlayClass, g_instance);

    g_renderCallback = nullptr;
    g_interactivityInitialized = false;
}

HWND D3D12Overlay_GetWindow() { return g_overlay; }
