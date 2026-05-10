#include "dinput8_proxy.h"
#include "logging.h"
#include <shlobj.h>

HMODULE DInput8Proxy::s_hRealDll = nullptr;
DInput8Proxy::PDirectInput8Create DInput8Proxy::oDirectInput8Create = nullptr;
DInput8Proxy::PDllCanUnloadNow DInput8Proxy::oDllCanUnloadNow = nullptr;
DInput8Proxy::PDllGetClassObject DInput8Proxy::oDllGetClassObject = nullptr;
DInput8Proxy::PDllRegisterServer DInput8Proxy::oDllRegisterServer = nullptr;
DInput8Proxy::PDllUnregisterServer DInput8Proxy::oDllUnregisterServer = nullptr;

bool DInput8Proxy::Initialize() {
    char sysPath[MAX_PATH];
    if (GetSystemDirectoryA(sysPath, MAX_PATH) == 0) return false;
    
    std::string realDllPath = std::string(sysPath) + "\\dinput8.dll";
    s_hRealDll = LoadLibraryA(realDllPath.c_str());
    
    if (!s_hRealDll) {
        LOG_ERR("Failed to load real dinput8.dll from " + realDllPath);
        return false;
    }

    oDirectInput8Create = (PDirectInput8Create)GetProcAddress(s_hRealDll, "DirectInput8Create");
    oDllCanUnloadNow = (PDllCanUnloadNow)GetProcAddress(s_hRealDll, "DllCanUnloadNow");
    oDllGetClassObject = (PDllGetClassObject)GetProcAddress(s_hRealDll, "DllGetClassObject");
    oDllRegisterServer = (PDllRegisterServer)GetProcAddress(s_hRealDll, "DllRegisterServer");
    oDllUnregisterServer = (PDllUnregisterServer)GetProcAddress(s_hRealDll, "DllUnregisterServer");

    LOG_INFO("Real dinput8.dll loaded and exports resolved");
    return true;
}

void DInput8Proxy::Shutdown() {
    if (s_hRealDll) {
        FreeLibrary(s_hRealDll);
        s_hRealDll = nullptr;
    }
}

// Actual export implementations for the linker
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
    if (!DInput8Proxy::oDirectInput8Create) return E_FAIL;
    return DInput8Proxy::oDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    if (!DInput8Proxy::oDllCanUnloadNow) return E_FAIL;
    return DInput8Proxy::oDllCanUnloadNow();
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!DInput8Proxy::oDllGetClassObject) return E_FAIL;
    return DInput8Proxy::oDllGetClassObject(rclsid, riid, ppv);
}

extern "C" HRESULT WINAPI DllRegisterServer() {
    if (!DInput8Proxy::oDllRegisterServer) return E_FAIL;
    return DInput8Proxy::oDllRegisterServer();
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
    if (!DInput8Proxy::oDllUnregisterServer) return E_FAIL;
    return DInput8Proxy::oDllUnregisterServer();
}
