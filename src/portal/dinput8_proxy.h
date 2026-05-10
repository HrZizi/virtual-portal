#pragma once
#include <windows.h>

class DInput8Proxy {
public:
    static bool Initialize();
    static void Shutdown();

    // Exported function type definitions
    typedef HRESULT (WINAPI *PDirectInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    typedef HRESULT (WINAPI *PDllCanUnloadNow)();
    typedef HRESULT (WINAPI *PDllGetClassObject)(REFCLSID, REFIID, LPVOID*);
    typedef HRESULT (WINAPI *PDllRegisterServer)();
    typedef HRESULT (WINAPI *PDllUnregisterServer)();

    static PDirectInput8Create oDirectInput8Create;
    static PDllCanUnloadNow oDllCanUnloadNow;
    static PDllGetClassObject oDllGetClassObject;
    static PDllRegisterServer oDllRegisterServer;
    static PDllUnregisterServer oDllUnregisterServer;

private:
    static HMODULE s_hRealDll;
};
