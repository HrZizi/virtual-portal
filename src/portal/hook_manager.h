#pragma once
#include <windows.h>
#include <winternl.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <string>
#include <Xinput.h>
#include <mmsystem.h>


// The dummy handle value used for the virtual portal
#define VIRTUAL_PORTAL_HANDLE (HANDLE)0x13370150

// Synthetic wide device path that we inject
#define VIRTUAL_PORTAL_PATH_W                                                  \
  L"\\\\?\\hid#vid_1430&pid_0150#virtual#portal_device"

class HookManager {
public:
  static void Install();
  static void Uninstall();
  static void SubmitGamepadNavigation();
  static bool CaptureDirectInputState(DWORD cbData, const void *data);
  static bool CaptureDirectInputButtonEvent(DWORD objectOffset, DWORD data);

  // --- Original function pointers ---

  // SetupDi — W variants (portal.dll uses W)
  typedef HDEVINFO(WINAPI *PSetupDiGetClassDevsW)(const GUID *, PCWSTR, HWND,
                                                  DWORD);
  typedef BOOL(WINAPI *PSetupDiEnumDeviceInterfaces)(HDEVINFO, PSP_DEVINFO_DATA,
                                                     const GUID *, DWORD,
                                                     PSP_DEVICE_INTERFACE_DATA);
  typedef BOOL(WINAPI *PSetupDiGetDeviceInterfaceDetailW)(
      HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W,
      DWORD, PDWORD, PSP_DEVINFO_DATA);
  typedef BOOL(WINAPI *PSetupDiDestroyDeviceInfoList)(HDEVINFO);

  // File I/O — W variant (portal.dll uses CreateFileW)
  typedef HANDLE(WINAPI *PCreateFileW)(LPCWSTR, DWORD, DWORD,
                                       LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                       HANDLE);
  typedef BOOL(WINAPI *PReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);

  // HID output — portal.dll uses HidD_SetOutputReport, NOT WriteFile
  typedef BOOL(WINAPI *PHidD_SetOutputReport)(HANDLE, PVOID, ULONG);
  typedef BOOL(WINAPI *PHidD_GetAttributes)(HANDLE, PHIDD_ATTRIBUTES);

  typedef BOOL(WINAPI *PCloseHandle)(HANDLE);

  // Input blackout hooks
  typedef DWORD(WINAPI *PXInputGetState)(DWORD, XINPUT_STATE *);
  typedef SHORT(WINAPI *PGetAsyncKeyState)(int);
  typedef SHORT(WINAPI *PGetKeyState)(int);
  typedef BOOL(WINAPI *PGetKeyboardState)(PBYTE);
  typedef BOOL(WINAPI *PPeekMessageA)(LPMSG, HWND, UINT, UINT, UINT);
  typedef BOOL(WINAPI *PGetMessageA)(LPMSG, HWND, UINT, UINT);
  typedef MMRESULT(WINAPI *PJoyGetPosEx)(UINT, LPJOYINFOEX);

  static PSetupDiGetClassDevsW oSetupDiGetClassDevsW;
  static PSetupDiEnumDeviceInterfaces oSetupDiEnumDeviceInterfaces;
  static PSetupDiGetDeviceInterfaceDetailW oSetupDiGetDeviceInterfaceDetailW;
  static PSetupDiDestroyDeviceInfoList oSetupDiDestroyDeviceInfoList;
  static PCreateFileW oCreateFileW;
  static PReadFile oReadFile;
  static PHidD_SetOutputReport oHidD_SetOutputReport;
  static PHidD_GetAttributes oHidD_GetAttributes;
  static PCloseHandle oCloseHandle;

  static PXInputGetState oXInputGetState;
  static PGetAsyncKeyState oGetAsyncKeyState;
  static PGetKeyState oGetKeyState;
  static PGetKeyboardState oGetKeyboardState;
  static PPeekMessageA oPeekMessageA;
  static PGetMessageA oGetMessageA;
  static PJoyGetPosEx oJoyGetPosEx;

private:
  // Hooked functions
  static HDEVINFO WINAPI Hooked_SetupDiGetClassDevsW(const GUID *ClassGuid,
                                                     PCWSTR Enumerator,
                                                     HWND hwndParent,
                                                     DWORD Flags);
  static BOOL WINAPI Hooked_SetupDiEnumDeviceInterfaces(
      HDEVINFO DeviceInfoSet, PSP_DEVINFO_DATA DeviceInfoData,
      const GUID *InterfaceClassGuid, DWORD MemberIndex,
      PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData);
  static BOOL WINAPI Hooked_SetupDiGetDeviceInterfaceDetailW(
      HDEVINFO DeviceInfoSet, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
      PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData,
      DWORD DeviceInterfaceDetailDataSize, PDWORD RequiredSize,
      PSP_DEVINFO_DATA DeviceInfoDataOut);
  static HANDLE WINAPI Hooked_CreateFileW(
      LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
      LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
      DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
  static BOOL WINAPI Hooked_ReadFile(HANDLE hFile, LPVOID lpBuffer,
                                     DWORD nNumberOfBytesToRead,
                                     LPDWORD lpNumberOfBytesRead,
                                     LPOVERLAPPED lpOverlapped);
  static BOOL WINAPI Hooked_HidD_SetOutputReport(HANDLE HidDeviceObject,
                                                 PVOID ReportBuffer,
                                                 ULONG ReportBufferLength);
  static BOOL WINAPI Hooked_HidD_GetAttributes(HANDLE HidDeviceObject,
                                               PHIDD_ATTRIBUTES Attributes);
  static BOOL WINAPI Hooked_CloseHandle(HANDLE hObject);

  static DWORD WINAPI Hooked_XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState);
  static SHORT WINAPI Hooked_GetAsyncKeyState(int vKey);
  static SHORT WINAPI Hooked_GetKeyState(int vKey);
  static BOOL WINAPI Hooked_GetKeyboardState(PBYTE lpKeyState);
  static BOOL WINAPI Hooked_PeekMessageA(LPMSG lpMsg, HWND hWnd,
                                         UINT wMsgFilterMin,
                                         UINT wMsgFilterMax, UINT wRemoveMsg);
  static BOOL WINAPI Hooked_GetMessageA(LPMSG lpMsg, HWND hWnd,
                                        UINT wMsgFilterMin,
                                        UINT wMsgFilterMax);
  static MMRESULT WINAPI Hooked_joyGetPosEx(UINT uJoyID, LPJOYINFOEX pji);
};
