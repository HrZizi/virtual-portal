#include "dinput8_proxy.h"
#include "hook_manager.h"
#include "imgui_manager.h"
#include "logging.h"
#include <MinHook.h>
#include <shlobj.h>

HMODULE DInput8Proxy::s_hRealDll = nullptr;
DInput8Proxy::PDirectInput8Create DInput8Proxy::oDirectInput8Create = nullptr;
DInput8Proxy::PDllCanUnloadNow DInput8Proxy::oDllCanUnloadNow = nullptr;
DInput8Proxy::PDllGetClassObject DInput8Proxy::oDllGetClassObject = nullptr;
DInput8Proxy::PDllRegisterServer DInput8Proxy::oDllRegisterServer = nullptr;
DInput8Proxy::PDllUnregisterServer DInput8Proxy::oDllUnregisterServer = nullptr;

DInput8Proxy::PCreateDevice DInput8Proxy::oCreateDevice = nullptr;
DInput8Proxy::PGetDeviceState DInput8Proxy::oGetDeviceState = nullptr;
DInput8Proxy::PGetDeviceData DInput8Proxy::oGetDeviceData = nullptr;

static void NeutralizeDirectInputState(DWORD cbData, LPVOID lpvData) {
  if (!lpvData || cbData == 0)
    return;

  ZeroMemory(lpvData, cbData);

  if (cbData == sizeof(DIJOYSTATE)) {
    DIJOYSTATE *state = static_cast<DIJOYSTATE *>(lpvData);
    for (DWORD &pov : state->rgdwPOV)
      pov = 0xFFFFFFFF;
  } else if (cbData == sizeof(DIJOYSTATE2)) {
    DIJOYSTATE2 *state = static_cast<DIJOYSTATE2 *>(lpvData);
    for (DWORD &pov : state->rgdwPOV)
      pov = 0xFFFFFFFF;
  }
}

bool DInput8Proxy::Initialize() {
  char sysPath[MAX_PATH];
  if (GetSystemDirectoryA(sysPath, MAX_PATH) == 0)
    return false;

  std::string realDllPath = std::string(sysPath) + "\\dinput8.dll";
  s_hRealDll = LoadLibraryA(realDllPath.c_str());

  if (!s_hRealDll) {
    LOG_ERR("Failed to load real dinput8.dll from " + realDllPath);
    return false;
  }

  oDirectInput8Create =
      (PDirectInput8Create)GetProcAddress(s_hRealDll, "DirectInput8Create");
  oDllCanUnloadNow =
      (PDllCanUnloadNow)GetProcAddress(s_hRealDll, "DllCanUnloadNow");
  oDllGetClassObject =
      (PDllGetClassObject)GetProcAddress(s_hRealDll, "DllGetClassObject");
  oDllRegisterServer =
      (PDllRegisterServer)GetProcAddress(s_hRealDll, "DllRegisterServer");
  oDllUnregisterServer =
      (PDllUnregisterServer)GetProcAddress(s_hRealDll, "DllUnregisterServer");

  LOG_INFO("Real dinput8.dll loaded and exports resolved");
  return true;
}

void DInput8Proxy::Shutdown() {
  if (s_hRealDll) {
    FreeLibrary(s_hRealDll);
    s_hRealDll = nullptr;
  }
}

// ---------------------------------------------------------------------------
// DirectInput Hooks
// ---------------------------------------------------------------------------

HRESULT WINAPI DInput8Proxy::Hooked_GetDeviceState(IDirectInputDevice8A *pDevice,
                                                   DWORD cbData,
                                                   LPVOID lpvData) {
  HRESULT hr = oGetDeviceState(pDevice, cbData, lpvData);
  bool toggledOverlay = false;
  if (SUCCEEDED(hr))
    toggledOverlay = HookManager::CaptureDirectInputState(cbData, lpvData);
  if (SUCCEEDED(hr) && (ImGuiManager::Get().IsVisible() || toggledOverlay)) {
    NeutralizeDirectInputState(cbData, lpvData);
  }
  return hr;
}

HRESULT WINAPI DInput8Proxy::Hooked_GetDeviceData(
    IDirectInputDevice8A *pDevice, DWORD cbObjectData,
    LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) {
  HRESULT hr = oGetDeviceData(pDevice, cbObjectData, rgdod, pdwInOut, dwFlags);
  bool toggledOverlay = false;
  if (SUCCEEDED(hr) && rgdod && pdwInOut) {
    for (DWORD i = 0; i < *pdwInOut; ++i) {
      toggledOverlay |=
          HookManager::CaptureDirectInputButtonEvent(rgdod[i].dwOfs,
                                                     rgdod[i].dwData);
    }
  }
  if (SUCCEEDED(hr) && (ImGuiManager::Get().IsVisible() || toggledOverlay)) {
    // Report zero events so the game sees no buffered input
    if (pdwInOut)
      *pdwInOut = 0;
  }
  return hr;
}

HRESULT WINAPI DInput8Proxy::Hooked_CreateDevice(
    IDirectInput8A *pThis, REFGUID rguid,
    LPDIRECTINPUTDEVICE8A *ppDirectInputDevice, LPUNKNOWN pUnkOuter) {
  HRESULT hr = oCreateDevice(pThis, rguid, ppDirectInputDevice, pUnkOuter);
  if (SUCCEEDED(hr) && ppDirectInputDevice && *ppDirectInputDevice) {
    IDirectInputDevice8A *pDevice = *ppDirectInputDevice;
    void **vTable = *(void ***)pDevice;

    if (!oGetDeviceState) {
      MH_STATUS status = MH_CreateHook(vTable[9], &Hooked_GetDeviceState,
                        (LPVOID *)&oGetDeviceState);
      if (status == MH_OK) {
        status = MH_EnableHook(vTable[9]);
        if (status == MH_OK) {
            LOG_INFO("Hooked IDirectInputDevice8::GetDeviceState");
        } else {
            LOG_ERR("Failed to enable IDirectInputDevice8::GetDeviceState hook (status: " + std::to_string(status) + ")");
        }
      } else {
        LOG_ERR("Failed to create IDirectInputDevice8::GetDeviceState hook (status: " + std::to_string(status) + ")");
      }
    }

    if (!oGetDeviceData) {
      MH_STATUS status = MH_CreateHook(vTable[10], &Hooked_GetDeviceData,
                        (LPVOID *)&oGetDeviceData);
      if (status == MH_OK) {
        status = MH_EnableHook(vTable[10]);
        if (status == MH_OK) {
            LOG_INFO("Hooked IDirectInputDevice8::GetDeviceData");
        } else {
            LOG_ERR("Failed to enable IDirectInputDevice8::GetDeviceData hook (status: " + std::to_string(status) + ")");
        }
      } else {
        LOG_ERR("Failed to create IDirectInputDevice8::GetDeviceData hook (status: " + std::to_string(status) + ")");
      }
    }
  }
  return hr;
}

// Actual export implementations for the linker
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion,
                                             REFIID riidltf, LPVOID *ppvOut,
                                             LPUNKNOWN punkOuter) {
  if (!DInput8Proxy::oDirectInput8Create)
    return E_FAIL;

  HRESULT hr = DInput8Proxy::oDirectInput8Create(hinst, dwVersion, riidltf,
                                                 ppvOut, punkOuter);
  if (SUCCEEDED(hr) && ppvOut && *ppvOut) {
    // We only hook if it's the A interface for simplicity, or we can check IID
    // Typically games use the A version.
    IDirectInput8A *pDI = (IDirectInput8A *)*ppvOut;
    void **vTable = *(void ***)pDI;

    if (!DInput8Proxy::oCreateDevice) {
      if (MH_CreateHook(vTable[3], &DInput8Proxy::Hooked_CreateDevice,
                        (LPVOID *)&DInput8Proxy::oCreateDevice) == MH_OK) {
        MH_EnableHook(vTable[3]);
        LOG_INFO("Hooked IDirectInput8::CreateDevice");
      }
    }
  }
  return hr;
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
  if (!DInput8Proxy::oDllCanUnloadNow)
    return E_FAIL;
  return DInput8Proxy::oDllCanUnloadNow();
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid,
                                            LPVOID *ppv) {
  if (!DInput8Proxy::oDllGetClassObject)
    return E_FAIL;
  return DInput8Proxy::oDllGetClassObject(rclsid, riid, ppv);
}

extern "C" HRESULT WINAPI DllRegisterServer() {
  if (!DInput8Proxy::oDllRegisterServer)
    return E_FAIL;
  return DInput8Proxy::oDllRegisterServer();
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
  if (!DInput8Proxy::oDllUnregisterServer)
    return E_FAIL;
  return DInput8Proxy::oDllUnregisterServer();
}
