#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

class DInput8Proxy {
public:
  static bool Initialize();
  static void Shutdown();

  // Exported function type definitions
  typedef HRESULT(WINAPI *PDirectInput8Create)(HINSTANCE, DWORD, REFIID,
                                               LPVOID *, LPUNKNOWN);
  typedef HRESULT(WINAPI *PDllCanUnloadNow)();
  typedef HRESULT(WINAPI *PDllGetClassObject)(REFCLSID, REFIID, LPVOID *);
  typedef HRESULT(WINAPI *PDllRegisterServer)();
  typedef HRESULT(WINAPI *PDllUnregisterServer)();

  static PDirectInput8Create oDirectInput8Create;
  static PDllCanUnloadNow oDllCanUnloadNow;
  static PDllGetClassObject oDllGetClassObject;
  static PDllRegisterServer oDllRegisterServer;
  static PDllUnregisterServer oDllUnregisterServer;

  // DirectInput VTable Hooks
  typedef HRESULT(WINAPI *PCreateDevice)(IDirectInput8A *, REFGUID,
                                         LPDIRECTINPUTDEVICE8A *, LPUNKNOWN);
  typedef HRESULT(WINAPI *PGetDeviceState)(IDirectInputDevice8A *, DWORD,
                                          LPVOID);
  typedef HRESULT(WINAPI *PGetDeviceData)(IDirectInputDevice8A *, DWORD,
                                         LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);

  static PCreateDevice oCreateDevice;
  static PGetDeviceState oGetDeviceState;
  static PGetDeviceData oGetDeviceData;

private:
  static HRESULT WINAPI Hooked_CreateDevice(IDirectInput8A *pThis, REFGUID rguid,
                                           LPDIRECTINPUTDEVICE8A *ppDirectInputDevice,
                                           LPUNKNOWN pUnkOuter);
  static HRESULT WINAPI Hooked_GetDeviceState(IDirectInputDevice8A *pDevice,
                                              DWORD cbData, LPVOID lpvData);
  static HRESULT WINAPI Hooked_GetDeviceData(IDirectInputDevice8A *pDevice,
                                             DWORD cbObjectData,
                                             LPDIDEVICEOBJECTDATA rgdod,
                                             LPDWORD pdwInOut, DWORD dwFlags);

  static HMODULE s_hRealDll;
};
