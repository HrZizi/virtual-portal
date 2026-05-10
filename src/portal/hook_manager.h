#pragma once
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <string>

// The dummy handle value used for the virtual portal
#define VIRTUAL_PORTAL_HANDLE (HANDLE)0x13370150

// Synthetic wide device path that we inject
#define VIRTUAL_PORTAL_PATH_W L"\\\\?\\hid#vid_1430&pid_0150#virtual#portal_device"

class HookManager {
public:
    static void Install();
    static void Uninstall();

    // --- Original function pointers ---

    // SetupDi — W variants (portal.dll uses W)
    typedef HDEVINFO (WINAPI *PSetupDiGetClassDevsW)(const GUID*, PCWSTR, HWND, DWORD);
    typedef BOOL (WINAPI *PSetupDiEnumDeviceInterfaces)(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD, PSP_DEVICE_INTERFACE_DATA);
    typedef BOOL (WINAPI *PSetupDiGetDeviceInterfaceDetailW)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD, PDWORD, PSP_DEVINFO_DATA);
    typedef BOOL (WINAPI *PSetupDiDestroyDeviceInfoList)(HDEVINFO);

    // File I/O — W variant (portal.dll uses CreateFileW)
    typedef HANDLE (WINAPI *PCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    typedef BOOL   (WINAPI *PReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);

    // HID output — portal.dll uses HidD_SetOutputReport, NOT WriteFile
    typedef BOOL (WINAPI *PHidD_SetOutputReport)(HANDLE, PVOID, ULONG);
    typedef BOOL (WINAPI *PHidD_GetAttributes)(HANDLE, PHIDD_ATTRIBUTES);

    typedef BOOL (WINAPI *PCloseHandle)(HANDLE);

    static PSetupDiGetClassDevsW        oSetupDiGetClassDevsW;
    static PSetupDiEnumDeviceInterfaces oSetupDiEnumDeviceInterfaces;
    static PSetupDiGetDeviceInterfaceDetailW oSetupDiGetDeviceInterfaceDetailW;
    static PSetupDiDestroyDeviceInfoList oSetupDiDestroyDeviceInfoList;
    static PCreateFileW                 oCreateFileW;
    static PReadFile                    oReadFile;
    static PHidD_SetOutputReport        oHidD_SetOutputReport;
    static PHidD_GetAttributes          oHidD_GetAttributes;
    static PCloseHandle                 oCloseHandle;

private:
    // Hooked functions
    static HDEVINFO WINAPI Hooked_SetupDiGetClassDevsW(const GUID* ClassGuid, PCWSTR Enumerator, HWND hwndParent, DWORD Flags);
    static BOOL WINAPI Hooked_SetupDiEnumDeviceInterfaces(HDEVINFO DeviceInfoSet, PSP_DEVINFO_DATA DeviceInfoData, const GUID* InterfaceClassGuid, DWORD MemberIndex, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData);
    static BOOL WINAPI Hooked_SetupDiGetDeviceInterfaceDetailW(HDEVINFO DeviceInfoSet, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData, PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData, DWORD DeviceInterfaceDetailDataSize, PDWORD RequiredSize, PSP_DEVINFO_DATA DeviceInfoDataOut);
    static HANDLE WINAPI Hooked_CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
    static BOOL WINAPI Hooked_ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
    static BOOL WINAPI Hooked_HidD_SetOutputReport(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength);
    static BOOL WINAPI Hooked_HidD_GetAttributes(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes);
    static BOOL WINAPI Hooked_CloseHandle(HANDLE hObject);
};
