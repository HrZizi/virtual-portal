#pragma once
#include <windows.h>
#include <d3d9.h>

class GraphicsHooks {
public:
    static void Install();

private:
    static IDirect3D9* WINAPI Hooked_Direct3DCreate9(UINT SDKVersion);
    static HRESULT WINAPI Hooked_CreateDevice(IDirect3D9* pThis, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface);
    static HRESULT WINAPI Hooked_Reset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
    static HRESULT WINAPI Hooked_EndScene(IDirect3DDevice9* pDevice);

    // Dynamic Resolution Hooks
    static HRESULT WINAPI Hooked_SetViewport(IDirect3DDevice9* pDevice, const D3DVIEWPORT9* pViewport);
    static HRESULT WINAPI Hooked_SetTransform(IDirect3DDevice9* pDevice, D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix);
    static HRESULT WINAPI Hooked_SetVertexShaderConstantF(IDirect3DDevice9* pDevice, UINT StartRegister, const float* pConstantData, UINT Vector4fCount);
    static HRESULT WINAPI Hooked_SetRenderTarget(IDirect3DDevice9* pDevice, DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget);

    static void ApplyBorderless(HWND hWnd);
    static void UpdatePresentationParameters(D3DPRESENT_PARAMETERS* pPP);

    // User32 Hooks to prevent the game from resizing back
    static BOOL WINAPI Hooked_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
    static LONG WINAPI Hooked_SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong);
    static LONG WINAPI Hooked_SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong);
    static BOOL WINAPI Hooked_ClipCursor(const RECT* lpRect);

    // WndProc to prevent minimization
    static LRESULT CALLBACK Hooked_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    typedef IDirect3D9*(WINAPI* PDirect3DCreate9)(UINT);
    typedef HRESULT(WINAPI* PCreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    typedef HRESULT(WINAPI* PReset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    typedef HRESULT(WINAPI* PEndScene)(IDirect3DDevice9*);
    
    typedef HRESULT(WINAPI* PSetViewport)(IDirect3DDevice9*, const D3DVIEWPORT9*);
    typedef HRESULT(WINAPI* PSetTransform)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);
    typedef HRESULT(WINAPI* PSetVertexShaderConstantF)(IDirect3DDevice9*, UINT, const float*, UINT);
    typedef HRESULT(WINAPI* PSetRenderTarget)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);

    typedef BOOL(WINAPI* PSetWindowPos)(HWND, HWND, int, int, int, int, UINT);
    typedef LONG(WINAPI* PSetWindowLongA)(HWND, int, LONG);
    typedef LONG(WINAPI* PSetWindowLongW)(HWND, int, LONG);
    typedef BOOL(WINAPI* PClipCursor)(const RECT*);

    static PDirect3DCreate9 oDirect3DCreate9;
    static PCreateDevice oCreateDevice;
    static PReset oReset;
    static PEndScene oEndScene;

    static PSetViewport oSetViewport;
    static PSetTransform oSetTransform;
    static PSetVertexShaderConstantF oSetVertexShaderConstantF;
    static PSetRenderTarget oSetRenderTarget;
    
    static PSetWindowPos oSetWindowPos;
    static PSetWindowLongA oSetWindowLongA;
    static PSetWindowLongW oSetWindowLongW;
    static PClipCursor oClipCursor;
    
    static WNDPROC oWndProc;
    static HWND g_hGameWindow;
};
