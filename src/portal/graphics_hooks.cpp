#include "graphics_hooks.h"
#include "config.h"
#include "MinHook.h"
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <cmath>
#include <vector>

static std::mutex g_gfxLogMutex;
static std::ofstream g_gfxLogFile;

static void GfxLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_gfxLogMutex);
    if (!g_gfxLogFile.is_open()) {
        g_gfxLogFile.open("skylanders_gfx.log", std::ios::out | std::ios::trunc);
    }
    
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);
    
    g_gfxLogFile << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") << " [INFO] " << msg << std::endl;
    g_gfxLogFile.flush();
}

GraphicsHooks::PDirect3DCreate9 GraphicsHooks::oDirect3DCreate9 = nullptr;
GraphicsHooks::PCreateDevice GraphicsHooks::oCreateDevice = nullptr;
GraphicsHooks::PReset GraphicsHooks::oReset = nullptr;
GraphicsHooks::PEndScene GraphicsHooks::oEndScene = nullptr;

GraphicsHooks::PSetViewport GraphicsHooks::oSetViewport = nullptr;
GraphicsHooks::PSetTransform GraphicsHooks::oSetTransform = nullptr;
GraphicsHooks::PSetVertexShaderConstantF GraphicsHooks::oSetVertexShaderConstantF = nullptr;
GraphicsHooks::PSetRenderTarget GraphicsHooks::oSetRenderTarget = nullptr;

GraphicsHooks::PSetWindowPos GraphicsHooks::oSetWindowPos = nullptr;
GraphicsHooks::PSetWindowLongA GraphicsHooks::oSetWindowLongA = nullptr;
GraphicsHooks::PSetWindowLongW GraphicsHooks::oSetWindowLongW = nullptr;
GraphicsHooks::PClipCursor GraphicsHooks::oClipCursor = nullptr;

WNDPROC GraphicsHooks::oWndProc = nullptr;
HWND GraphicsHooks::g_hGameWindow = nullptr;

static float g_screenWidth = 0.0f;
static float g_screenHeight = 0.0f;

static float g_currentRTWidth = 0.0f;
static float g_currentRTHeight = 0.0f;

static RECT g_desiredClipRect = {0};
static bool g_hasClipRect = false;

LRESULT CALLBACK GraphicsHooks::Hooked_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // ImGui input handling (must come first so INSERT toggle works)
    if (ImGuiManager::Get().HandleWndProc(hWnd, uMsg, wParam, lParam))
        return true;

    if (Config::Get().GetBool("graphics", "borderless_window", false) && hWnd == g_hGameWindow) {
        switch (uMsg) {
            case WM_ACTIVATE: if (LOWORD(wParam) == WA_INACTIVE) return 0; break;
            case WM_ACTIVATEAPP: if (wParam == FALSE) return 0; break;
            case WM_KILLFOCUS: return 0;
            case WM_SYSCOMMAND: if ((wParam & 0xFFF0) == SC_MINIMIZE) return 0; break;
        }
    }
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void GraphicsHooks::ApplyBorderless(HWND hWnd) {
    if (!hWnd) return;
    g_hGameWindow = hWnd;
    if (oWndProc == nullptr) {
        oWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)Hooked_WndProc);
    }
    SetWindowLong(hWnd, GWL_STYLE, WS_VISIBLE | WS_POPUP);
    SetWindowPos(hWnd, HWND_TOP, 0, 0, (int)g_screenWidth, (int)g_screenHeight, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void GraphicsHooks::UpdatePresentationParameters(D3DPRESENT_PARAMETERS* pPP) {
    if (Config::Get().GetBool("graphics", "borderless_window", false)) {
        pPP->Windowed = TRUE;
        pPP->BackBufferWidth = GetSystemMetrics(SM_CXSCREEN);
        pPP->BackBufferHeight = GetSystemMetrics(SM_CYSCREEN);
        pPP->FullScreen_RefreshRateInHz = 0;
        pPP->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        
        g_screenWidth = (float)pPP->BackBufferWidth;
        g_screenHeight = (float)pPP->BackBufferHeight;
        g_currentRTWidth = g_screenWidth;
        g_currentRTHeight = g_screenHeight;
    }
}

HRESULT WINAPI GraphicsHooks::Hooked_SetRenderTarget(IDirect3DDevice9* pDevice, DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) {
    if (RenderTargetIndex == 0 && pRenderTarget) {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(pRenderTarget->GetDesc(&desc))) {
            g_currentRTWidth = (float)desc.Width;
            g_currentRTHeight = (float)desc.Height;
        }
    }
    return oSetRenderTarget(pDevice, RenderTargetIndex, pRenderTarget);
}

HRESULT WINAPI GraphicsHooks::Hooked_SetViewport(IDirect3DDevice9* pDevice, const D3DVIEWPORT9* pViewport) {
    return oSetViewport(pDevice, pViewport);
}

HRESULT WINAPI GraphicsHooks::Hooked_SetTransform(IDirect3DDevice9* pDevice, D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) {
    if (State == D3DTS_PROJECTION && pMatrix && g_screenHeight > 0) {
        bool isOrtho = std::abs(pMatrix->_34) < 0.001f && std::abs(pMatrix->_44 - 1.0f) < 0.001f;
        
        // Ensure we are rendering to the main screen and not a shadow map
        bool isMainScreen = (g_currentRTHeight == g_screenHeight && g_currentRTWidth == g_screenWidth);
        bool isUI = isOrtho && isMainScreen && std::abs(pMatrix->_11) < 0.1f;

        if (isUI) {
            RECT scissor = { 
                (LONG)((g_screenWidth - g_screenHeight * (16.0/9.0)) / 2), 
                0, 
                (LONG)((g_screenWidth + g_screenHeight * (16.0/9.0)) / 2), 
                (LONG)g_screenHeight 
            };
            pDevice->SetScissorRect(&scissor);
            pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        } else {
            pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        }
    }
    return oSetTransform(pDevice, State, pMatrix);
}

HRESULT WINAPI GraphicsHooks::Hooked_SetVertexShaderConstantF(IDirect3DDevice9* pDevice, UINT StartRegister, const float* pConstantData, UINT Vector4fCount) {
    if (pConstantData && Vector4fCount >= 4 && g_screenHeight > 0) {
        for (UINT i = 0; i <= Vector4fCount - 4; ++i) {
            const float* m = &pConstantData[i * 4];
            
            bool isPerspective = (std::abs(m[14] - 1.0f) < 0.01f || std::abs(m[14] + 1.0f) < 0.01f) && std::abs(m[15]) < 0.01f;
            bool isOrtho = std::abs(m[14]) < 0.01f && std::abs(m[15] - 1.0f) < 0.01f;
            
            if (!isPerspective && !isOrtho) {
                isPerspective = (std::abs(m[11] - 1.0f) < 0.01f || std::abs(m[11] + 1.0f) < 0.01f) && std::abs(m[15]) < 0.01f;
                isOrtho = std::abs(m[11]) < 0.01f && std::abs(m[15] - 1.0f) < 0.01f;
            }

            // Fingerprint check: UI vs Full-Screen Quads vs Shadow Maps
            bool isMainScreen = (g_currentRTHeight == g_screenHeight && g_currentRTWidth == g_screenWidth);
            bool isUI = isOrtho && isMainScreen && std::abs(m[0]) < 0.1f;

            if (isUI) {
                RECT scissor = { 
                    (LONG)((g_screenWidth - g_screenHeight * (16.0/9.0)) / 2), 
                    0, 
                    (LONG)((g_screenWidth + g_screenHeight * (16.0/9.0)) / 2), 
                    (LONG)g_screenHeight 
                };
                pDevice->SetScissorRect(&scissor);
                pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
            } else if (isPerspective || (isOrtho && !isUI)) {
                // Explicitly disable scissor for 3D passes, post-processing, and shadow maps
                pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            }
        }
    }
    return oSetVertexShaderConstantF(pDevice, StartRegister, pConstantData, Vector4fCount);
}

IDirect3D9* WINAPI GraphicsHooks::Hooked_Direct3DCreate9(UINT SDKVersion) {
    IDirect3D9* pD3D = oDirect3DCreate9(SDKVersion);
    if (pD3D) {
        static bool hooksInstalled = false;
        if (!hooksInstalled) {
            void** vtable = *(void***)pD3D;
            MH_CreateHook(vtable[16], &Hooked_CreateDevice, reinterpret_cast<LPVOID*>(&oCreateDevice));
            MH_EnableHook(vtable[16]);
            hooksInstalled = true;
        }
    }
    return pD3D;
}

HRESULT WINAPI GraphicsHooks::Hooked_CreateDevice(IDirect3D9* pThis, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) {
    UpdatePresentationParameters(pPresentationParameters);
    HRESULT hr = oCreateDevice(pThis, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    if (SUCCEEDED(hr)) {
        HWND targetWnd = hFocusWindow ? hFocusWindow : pPresentationParameters->hDeviceWindow;
        ApplyBorderless(targetWnd);
        
        void** vTable = *(void***)(*ppReturnedDeviceInterface);
        if (oReset == nullptr) {
            MH_CreateHook(vTable[16], &Hooked_Reset, reinterpret_cast<LPVOID*>(&oReset));
            MH_EnableHook(vTable[16]);
        }
        if (oEndScene == nullptr) {
            MH_CreateHook(vTable[42], &Hooked_EndScene, reinterpret_cast<LPVOID*>(&oEndScene));
            MH_EnableHook(vTable[42]);
        }
        if (oSetViewport == nullptr) {
            MH_CreateHook(vTable[47], &Hooked_SetViewport, reinterpret_cast<LPVOID*>(&oSetViewport));
            MH_EnableHook(vTable[47]);
        }
        if (oSetTransform == nullptr) {
            MH_CreateHook(vTable[44], &Hooked_SetTransform, reinterpret_cast<LPVOID*>(&oSetTransform));
            MH_EnableHook(vTable[44]);
        }
        if (oSetVertexShaderConstantF == nullptr) {
            MH_CreateHook(vTable[94], &Hooked_SetVertexShaderConstantF, reinterpret_cast<LPVOID*>(&oSetVertexShaderConstantF));
            MH_EnableHook(vTable[94]);
        }
        if (oSetRenderTarget == nullptr) {
            MH_CreateHook(vTable[37], &Hooked_SetRenderTarget, reinterpret_cast<LPVOID*>(&oSetRenderTarget));
            MH_EnableHook(vTable[37]);
        }
        GfxLog("RT-Aware UI Scissor hooks installed");

        // Initialize ImGui overlay
        HWND imguiWnd = hFocusWindow ? hFocusWindow : pPresentationParameters->hDeviceWindow;
        ImGuiManager::Get().Initialize(*ppReturnedDeviceInterface, imguiWnd);
    }
    return hr;
}

HRESULT WINAPI GraphicsHooks::Hooked_Reset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    // Invalidate ImGui device objects before Reset
    ImGuiManager::Get().InvalidateDeviceObjects();

    UpdatePresentationParameters(pPresentationParameters);
    HRESULT hr = oReset(pDevice, pPresentationParameters);
    if (SUCCEEDED(hr)) {
        // Recreate ImGui device objects after Reset
        ImGuiManager::Get().CreateDeviceObjects();

        D3DDEVICE_CREATION_PARAMETERS cp;
        if (SUCCEEDED(pDevice->GetCreationParameters(&cp))) {
            ApplyBorderless(cp.hFocusWindow);
        }
    }
    return hr;
}

HRESULT WINAPI GraphicsHooks::Hooked_EndScene(IDirect3DDevice9* pDevice) {
    static HWND lastForegroundWnd = NULL;
    HWND currentForegroundWnd = GetForegroundWindow();
    
    if (currentForegroundWnd != lastForegroundWnd) {
        lastForegroundWnd = currentForegroundWnd;
        if (currentForegroundWnd != g_hGameWindow) {
            // Game lost focus, free cursor
            if (oClipCursor) oClipCursor(NULL);
        } else if (g_hasClipRect) {
            // Game gained focus, restore its desired clip rect
            if (oClipCursor) oClipCursor(&g_desiredClipRect);
        }
    }

    // Render ImGui overlay (before EndScene returns to Present)
    ImGuiManager::Get().Render(pDevice);

    return oEndScene(pDevice);
}

BOOL WINAPI GraphicsHooks::Hooked_ClipCursor(const RECT* lpRect) {
    if (lpRect) {
        g_desiredClipRect = *lpRect;
        g_hasClipRect = true;
    } else {
        g_hasClipRect = false;
    }

    if (Config::Get().GetBool("graphics", "borderless_window", false) && g_hGameWindow) {
        if (GetForegroundWindow() != g_hGameWindow) {
            // We don't have focus. Always free the cursor.
            return oClipCursor(NULL);
        }
    }
    return oClipCursor(lpRect);
}

BOOL WINAPI GraphicsHooks::Hooked_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    if (Config::Get().GetBool("graphics", "borderless_window", false) && hWnd == g_hGameWindow) {
        X = 0; Y = 0;
        cx = (int)g_screenWidth;
        cy = (int)g_screenHeight;
        uFlags &= ~SWP_NOMOVE;
        uFlags &= ~SWP_NOSIZE;
    }
    return oSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

LONG WINAPI GraphicsHooks::Hooked_SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong) {
    if (Config::Get().GetBool("graphics", "borderless_window", false) && hWnd == g_hGameWindow && nIndex == GWL_STYLE) {
        dwNewLong = WS_VISIBLE | WS_POPUP;
    }
    return oSetWindowLongA(hWnd, nIndex, dwNewLong);
}

LONG WINAPI GraphicsHooks::Hooked_SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong) {
    if (Config::Get().GetBool("graphics", "borderless_window", false) && hWnd == g_hGameWindow && nIndex == GWL_STYLE) {
        dwNewLong = WS_VISIBLE | WS_POPUP;
    }
    return oSetWindowLongW(hWnd, nIndex, dwNewLong);
}

void GraphicsHooks::Install() {
    GfxLog("GraphicsHooks::Install - Initializing RT-Aware UI hooks");
    MH_CreateHookApi(L"d3d9.dll", "Direct3DCreate9", &Hooked_Direct3DCreate9, reinterpret_cast<LPVOID*>(&oDirect3DCreate9));
    MH_CreateHookApi(L"user32.dll", "SetWindowPos", &Hooked_SetWindowPos, reinterpret_cast<LPVOID*>(&oSetWindowPos));
    MH_CreateHookApi(L"user32.dll", "SetWindowLongA", &Hooked_SetWindowLongA, reinterpret_cast<LPVOID*>(&oSetWindowLongA));
    MH_CreateHookApi(L"user32.dll", "SetWindowLongW", &Hooked_SetWindowLongW, reinterpret_cast<LPVOID*>(&oSetWindowLongW));
    MH_CreateHookApi(L"user32.dll", "ClipCursor", &Hooked_ClipCursor, reinterpret_cast<LPVOID*>(&oClipCursor));
    MH_EnableHook(MH_ALL_HOOKS);
}
