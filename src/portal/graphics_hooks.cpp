#include "graphics_hooks.h"
#include "config.h"
#include "MinHook.h"
#include <string>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <mutex>

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

GraphicsHooks::PSetWindowPos GraphicsHooks::oSetWindowPos = nullptr;
GraphicsHooks::PSetWindowLongA GraphicsHooks::oSetWindowLongA = nullptr;
GraphicsHooks::PSetWindowLongW GraphicsHooks::oSetWindowLongW = nullptr;

WNDPROC GraphicsHooks::oWndProc = nullptr;
HWND GraphicsHooks::g_hGameWindow = nullptr;

LRESULT CALLBACK GraphicsHooks::Hooked_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (Config::Get().GetBool("graphics", "borderless_window", false) && hWnd == g_hGameWindow) {
        switch (uMsg) {
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE) {
                    return 0;
                }
                break;
            case WM_ACTIVATEAPP:
                if (wParam == FALSE) {
                    return 0;
                }
                break;
            case WM_KILLFOCUS:
                return 0;
            case WM_SYSCOMMAND:
                if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                    GfxLog("Blocked SC_MINIMIZE command");
                    return 0;
                }
                break;
        }
    }
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void GraphicsHooks::ApplyBorderless(HWND hWnd) {
    if (!hWnd) return;
    
    g_hGameWindow = hWnd;
    GfxLog("Enforcing borderless window styles for HWND: " + std::to_string((uintptr_t)hWnd));
    
    if (oWndProc == nullptr) {
        oWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)Hooked_WndProc);
        GfxLog("Window subclassed to prevent minimization");
    }
    
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    
    SetWindowLong(hWnd, GWL_STYLE, WS_VISIBLE | WS_POPUP);
    SetWindowPos(hWnd, HWND_TOP, 0, 0, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void GraphicsHooks::UpdatePresentationParameters(D3DPRESENT_PARAMETERS* pPP) {
    if (Config::Get().GetBool("graphics", "borderless_window", false)) {
        pPP->Windowed = TRUE;
        pPP->BackBufferWidth = GetSystemMetrics(SM_CXSCREEN);
        pPP->BackBufferHeight = GetSystemMetrics(SM_CYSCREEN);
        pPP->FullScreen_RefreshRateInHz = 0;
        pPP->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        GfxLog("Presentation Parameters updated: " + std::to_string(pPP->BackBufferWidth) + "x" + std::to_string(pPP->BackBufferHeight));
    }
}

IDirect3D9* WINAPI GraphicsHooks::Hooked_Direct3DCreate9(UINT SDKVersion) {
    GfxLog("Direct3DCreate9 intercepted");
    IDirect3D9* pD3D = oDirect3DCreate9(SDKVersion);
    if (pD3D) {
        static bool hooksInstalled = false;
        if (!hooksInstalled) {
            void** vtable = *(void***)pD3D;
            MH_CreateHook(vtable[16], &Hooked_CreateDevice, reinterpret_cast<LPVOID*>(&oCreateDevice));
            MH_EnableHook(vtable[16]);
            hooksInstalled = true;
            GfxLog("IDirect3D9::CreateDevice hook installed");
        }
    }
    return pD3D;
}

HRESULT WINAPI GraphicsHooks::Hooked_CreateDevice(IDirect3D9* pThis, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) {
    GfxLog("IDirect3D9::CreateDevice intercepted");
    UpdatePresentationParameters(pPresentationParameters);
    HRESULT hr = oCreateDevice(pThis, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    if (SUCCEEDED(hr)) {
        HWND targetWnd = hFocusWindow ? hFocusWindow : pPresentationParameters->hDeviceWindow;
        ApplyBorderless(targetWnd);
        
        void** deviceVTable = *(void***)(*ppReturnedDeviceInterface);
        if (oReset == nullptr) {
            MH_CreateHook(deviceVTable[16], &Hooked_Reset, reinterpret_cast<LPVOID*>(&oReset));
            MH_EnableHook(deviceVTable[16]);
        }
        if (oEndScene == nullptr) {
            MH_CreateHook(deviceVTable[42], &Hooked_EndScene, reinterpret_cast<LPVOID*>(&oEndScene));
            MH_EnableHook(deviceVTable[42]);
        }
        GfxLog("IDirect3DDevice9 hooks (Reset, EndScene) installed");
    }
    return hr;
}

HRESULT WINAPI GraphicsHooks::Hooked_Reset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    GfxLog("IDirect3DDevice9::Reset intercepted");
    UpdatePresentationParameters(pPresentationParameters);
    HRESULT hr = oReset(pDevice, pPresentationParameters);
    if (SUCCEEDED(hr)) {
        D3DDEVICE_CREATION_PARAMETERS cp;
        if (SUCCEEDED(pDevice->GetCreationParameters(&cp))) {
            ApplyBorderless(cp.hFocusWindow);
        }
    }
    return hr;
}

HRESULT WINAPI GraphicsHooks::Hooked_EndScene(IDirect3DDevice9* pDevice) {
    static bool firstFrame = true;
    if (firstFrame) {
        GfxLog("First frame rendered via EndScene hook");
        firstFrame = false;
    }
    return oEndScene(pDevice);
}

BOOL WINAPI GraphicsHooks::Hooked_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    if (Config::Get().GetBool("graphics", "borderless_window", false) && hWnd == g_hGameWindow) {
        X = 0; Y = 0;
        cx = GetSystemMetrics(SM_CXSCREEN);
        cy = GetSystemMetrics(SM_CYSCREEN);
        uFlags &= ~SWP_NOMOVE;
        uFlags &= ~SWP_NOSIZE;
    }
    return oSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

LONG WINAPI GraphicsHooks::Hooked_SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong) {
    if (Config::Get().GetBool("graphics", "borderless_window", false) && hWnd == g_hGameWindow) {
        if (nIndex == GWL_STYLE) {
            dwNewLong = WS_VISIBLE | WS_POPUP;
        }
    }
    return oSetWindowLongA(hWnd, nIndex, dwNewLong);
}

LONG WINAPI GraphicsHooks::Hooked_SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong) {
    if (Config::Get().GetBool("graphics", "borderless_window", false) && hWnd == g_hGameWindow) {
        if (nIndex == GWL_STYLE) {
            dwNewLong = WS_VISIBLE | WS_POPUP;
        }
    }
    return oSetWindowLongW(hWnd, nIndex, dwNewLong);
}

void GraphicsHooks::Install() {
    GfxLog("GraphicsHooks::Install - Restoring direct Direct3DCreate9 hooking");
    MH_CreateHookApi(L"d3d9.dll", "Direct3DCreate9", &Hooked_Direct3DCreate9, reinterpret_cast<LPVOID*>(&oDirect3DCreate9));
    
    MH_CreateHookApi(L"user32.dll", "SetWindowPos", &Hooked_SetWindowPos, reinterpret_cast<LPVOID*>(&oSetWindowPos));
    MH_CreateHookApi(L"user32.dll", "SetWindowLongA", &Hooked_SetWindowLongA, reinterpret_cast<LPVOID*>(&oSetWindowLongA));
    MH_CreateHookApi(L"user32.dll", "SetWindowLongW", &Hooked_SetWindowLongW, reinterpret_cast<LPVOID*>(&oSetWindowLongW));
    
    MH_EnableHook(MH_ALL_HOOKS);
}
