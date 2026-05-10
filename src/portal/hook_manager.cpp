#include "hook_manager.h"
#include "MinHook.h"
#include "portal_emulator.h"
#include "logging.h"
#include <cstring>
#include <set>
#include <mutex>

HookManager::PSetupDiGetClassDevsW             HookManager::oSetupDiGetClassDevsW             = nullptr;
HookManager::PSetupDiEnumDeviceInterfaces      HookManager::oSetupDiEnumDeviceInterfaces      = nullptr;
HookManager::PSetupDiGetDeviceInterfaceDetailW HookManager::oSetupDiGetDeviceInterfaceDetailW = nullptr;
HookManager::PSetupDiDestroyDeviceInfoList     HookManager::oSetupDiDestroyDeviceInfoList     = nullptr;
HookManager::PCreateFileW                      HookManager::oCreateFileW                      = nullptr;
HookManager::PReadFile                         HookManager::oReadFile                         = nullptr;
HookManager::PHidD_SetOutputReport             HookManager::oHidD_SetOutputReport             = nullptr;
HookManager::PHidD_GetAttributes               HookManager::oHidD_GetAttributes               = nullptr;
HookManager::PCloseHandle                      HookManager::oCloseHandle                      = nullptr;

// =============================================================================
// Handle tracking
//
// Using a fixed sentinel handle value caused collisions with real system
// handles, leading to spurious CloseHandle resets. Instead we track every
// handle we fabricate in a set.
// =============================================================================
static std::set<HANDLE> g_ourHandles;
static std::mutex        g_handleMutex;

static HANDLE MakeVirtualHandle() {
    // Allocate a real event handle so GetOverlappedResult / WaitForSingleObject
    // called by portal.dll's reading thread work without blocking forever.
    HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!h || h == INVALID_HANDLE_VALUE) {
        // Fallback: use a fixed magic value (should not happen in practice)
        h = (HANDLE)(ULONG_PTR)0x13370150;
    }
    std::lock_guard<std::mutex> lk(g_handleMutex);
    g_ourHandles.insert(h);
    return h;
}

static bool IsOurHandle(HANDLE h) {
    std::lock_guard<std::mutex> lk(g_handleMutex);
    return g_ourHandles.count(h) > 0;
}

static void ReleaseVirtualHandle(HANDLE h) {
    {
        std::lock_guard<std::mutex> lk(g_handleMutex);
        g_ourHandles.erase(h);
    }
    // Close the underlying event so we don't leak kernel objects
    if (h && h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
}

// =============================================================================
// Injection state — only needs to track whether we've already injected this
// enumeration pass (to avoid double-injection). The handle set replaces all
// the PROBED/CONFIRMED complexity.
// =============================================================================
static bool g_injectedThisPass = false;
static bool g_sessionOpen     = false;  // true while game holds a real SESSION handle

// Returns true if the wide path belongs to a portal device (matches VID+PID)
static bool IsPortalPath(LPCWSTR path) {
    if (!path) return false;
    // Match our injected synthetic path OR any real portal path
    return (wcsstr(path, L"vid_1430") != nullptr && wcsstr(path, L"pid_0150") != nullptr)
        || (wcsstr(path, L"VID_1430") != nullptr && wcsstr(path, L"PID_0150") != nullptr);
}

// =============================================================================
void HookManager::Install() {
    if (MH_Initialize() != MH_OK) { LOG_ERR("MinHook failed to initialize"); return; }

    MH_CreateHookApi(L"setupapi", "SetupDiGetClassDevsW",             &Hooked_SetupDiGetClassDevsW,             reinterpret_cast<LPVOID*>(&oSetupDiGetClassDevsW));
    MH_CreateHookApi(L"setupapi", "SetupDiEnumDeviceInterfaces",      &Hooked_SetupDiEnumDeviceInterfaces,      reinterpret_cast<LPVOID*>(&oSetupDiEnumDeviceInterfaces));
    MH_CreateHookApi(L"setupapi", "SetupDiGetDeviceInterfaceDetailW", &Hooked_SetupDiGetDeviceInterfaceDetailW, reinterpret_cast<LPVOID*>(&oSetupDiGetDeviceInterfaceDetailW));
    MH_CreateHookApi(L"kernel32", "CreateFileW",                      &Hooked_CreateFileW,                      reinterpret_cast<LPVOID*>(&oCreateFileW));
    MH_CreateHookApi(L"kernel32", "ReadFile",                         &Hooked_ReadFile,                         reinterpret_cast<LPVOID*>(&oReadFile));
    MH_CreateHookApi(L"hid",      "HidD_SetOutputReport",             &Hooked_HidD_SetOutputReport,             reinterpret_cast<LPVOID*>(&oHidD_SetOutputReport));
    MH_CreateHookApi(L"hid",      "HidD_GetAttributes",               &Hooked_HidD_GetAttributes,               reinterpret_cast<LPVOID*>(&oHidD_GetAttributes));
    MH_CreateHookApi(L"kernel32", "CloseHandle",                      &Hooked_CloseHandle,                      reinterpret_cast<LPVOID*>(&oCloseHandle));

    MH_EnableHook(MH_ALL_HOOKS);
    LOG_INFO("Hooks installed (handle-set tracking)");
}

void HookManager::Uninstall() {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    LOG_INFO("Hooks uninstalled");
}

// =============================================================================
// SetupDiGetClassDevsW — new enumeration pass starts; reset injection flag
// =============================================================================
HDEVINFO WINAPI HookManager::Hooked_SetupDiGetClassDevsW(
    const GUID* ClassGuid, PCWSTR Enumerator, HWND hwndParent, DWORD Flags)
{
    g_injectedThisPass = false;
    // Don't reset g_sessionOpen here — the game polls SetupDi even while
    // the portal is open (device-change monitoring). Only CloseHandle resets it.
    return oSetupDiGetClassDevsW(ClassGuid, Enumerator, hwndParent, Flags);
}

// =============================================================================
// SetupDiEnumDeviceInterfaces
// Inject exactly once per enumeration pass when real devices are exhausted.
// =============================================================================
BOOL WINAPI HookManager::Hooked_SetupDiEnumDeviceInterfaces(
    HDEVINFO DeviceInfoSet, PSP_DEVINFO_DATA DeviceInfoData,
    const GUID* InterfaceClassGuid, DWORD MemberIndex,
    PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData)
{
    BOOL result = oSetupDiEnumDeviceInterfaces(
        DeviceInfoSet, DeviceInfoData, InterfaceClassGuid,
        MemberIndex, DeviceInterfaceData);

    if (!result && GetLastError() == ERROR_NO_MORE_ITEMS && !g_injectedThisPass && !g_sessionOpen) {
        g_injectedThisPass = true;

        ZeroMemory(DeviceInterfaceData, sizeof(SP_DEVICE_INTERFACE_DATA));
        DeviceInterfaceData->cbSize             = sizeof(SP_DEVICE_INTERFACE_DATA);
        DeviceInterfaceData->InterfaceClassGuid = *InterfaceClassGuid;
        DeviceInterfaceData->Flags              = 0x1337;
        DeviceInterfaceData->Reserved           = 0x1337;

        SetLastError(0);
        LOG_INFO("Virtual portal injected at MemberIndex " + std::to_string(MemberIndex));
        return TRUE;
    }

    return result;
}

// =============================================================================
// SetupDiGetDeviceInterfaceDetailW — return synthetic path for our sentinel
// =============================================================================
BOOL WINAPI HookManager::Hooked_SetupDiGetDeviceInterfaceDetailW(
    HDEVINFO DeviceInfoSet,
    PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData,
    DWORD DeviceInterfaceDetailDataSize,
    PDWORD RequiredSize,
    PSP_DEVINFO_DATA DeviceInfoDataOut)
{
    if (DeviceInterfaceData && DeviceInterfaceData->Flags == 0x1337) {
        const wchar_t* path  = VIRTUAL_PORTAL_PATH_W;
        DWORD pathBytes = (DWORD)((wcslen(path) + 1) * sizeof(wchar_t));
        DWORD needed    = offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath) + pathBytes;

        if (RequiredSize) *RequiredSize = needed;

        if (!DeviceInterfaceDetailData || DeviceInterfaceDetailDataSize < needed) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }

        DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        wcscpy_s(DeviceInterfaceDetailData->DevicePath,
                 (DeviceInterfaceDetailDataSize
                  - offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath))
                 / sizeof(wchar_t),
                 path);

        if (DeviceInfoDataOut && DeviceInfoDataOut->cbSize > 0)
            ZeroMemory(DeviceInfoDataOut, DeviceInfoDataOut->cbSize);

        return TRUE;
    }

    return oSetupDiGetDeviceInterfaceDetailW(
        DeviceInfoSet, DeviceInterfaceData, DeviceInterfaceDetailData,
        DeviceInterfaceDetailDataSize, RequiredSize, DeviceInfoDataOut);
}

// =============================================================================
// CreateFileW
//
// Intercept ANY path that looks like our portal (contains vid_1430 & pid_0150).
// Log the exact path received so we can debug future mismatches.
// Return a real event handle so overlapped I/O calls don't deadlock.
// =============================================================================
HANDLE WINAPI HookManager::Hooked_CreateFileW(
    LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    // Convert wide path to narrow for logging
    auto wToA = [](LPCWSTR w) -> std::string {
        if (!w) return "";
        int n = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_ACP, 0, w, -1, &s[0], n, nullptr, nullptr);
        if (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    };

    if (lpFileName) {
        std::string narrowPath = wToA(lpFileName);
        // Log any path containing "hid#" to see what the probe is actually opening
        if (narrowPath.find("hid#") != std::string::npos || narrowPath.find("HID#") != std::string::npos) {
            char buf[16]; sprintf_s(buf, "%08X", dwDesiredAccess);
            LOG_INFO("CreateFileW INTERCEPT access=0x" + std::string(buf) + " path=" + narrowPath);
        }
    }

    if (lpFileName && IsPortalPath(lpFileName)) {
        HANDLE h = MakeVirtualHandle();
        if (dwDesiredAccess != 0) {
            // Real SESSION open — mark portal as active so we stop re-injecting
            g_sessionOpen = true;
        }
        
        if (dwDesiredAccess == 0) {
            LOG_INFO("CreateFileW PROBE   path=" + wToA(lpFileName) +
                     " handle=" + std::to_string((ULONG_PTR)h));
        } else {
            char buf[16]; sprintf_s(buf, "%08X", dwDesiredAccess);
            LOG_INFO("CreateFileW SESSION access=0x" + std::string(buf) +
                     " path=" + wToA(lpFileName) +
                     " handle=" + std::to_string((ULONG_PTR)h));
        }
        return h;
    }

    return oCreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                        lpSecurityAttributes, dwCreationDisposition,
                        dwFlagsAndAttributes, hTemplateFile);
}

// =============================================================================
// ReadFile — serve queued portal IN responses
// Uses OVERLAPPED signalling so portal.dll's GetOverlappedResult succeeds.
// =============================================================================
BOOL WINAPI HookManager::Hooked_ReadFile(
    HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    if (!IsOurHandle(hFile))
        return oReadFile(hFile, lpBuffer, nNumberOfBytesToRead,
                         lpNumberOfBytesRead, lpOverlapped);

    uint8_t resp[32] = {0};
    if (!PortalEmulator::Get().GetNextResponse(resp)) {
        PortalEmulator::Get().UpdateStatus(); // rate-limited to 50 Hz internally
        PortalEmulator::Get().GetNextResponse(resp);
    }

    // For HID devices, the first byte of the read buffer is the Report ID (usually 0).
    // The actual payload starts at offset 1.
    if (nNumberOfBytesToRead > 0) {
        static_cast<uint8_t*>(lpBuffer)[0] = 0; // Report ID
        
        DWORD toCopy = min(nNumberOfBytesToRead - 1, 32u);
        if (toCopy > 0) {
            std::memcpy(static_cast<uint8_t*>(lpBuffer) + 1, resp, toCopy);
        }
        
        if (lpNumberOfBytesRead) *lpNumberOfBytesRead = toCopy + 1;
        
        if (lpOverlapped) {
            lpOverlapped->Internal     = 0;          // STATUS_SUCCESS
            lpOverlapped->InternalHigh = toCopy + 1;
            SetEvent(hFile);
            if (lpOverlapped->hEvent) SetEvent(lpOverlapped->hEvent);
        }
    } else {
        if (lpNumberOfBytesRead) *lpNumberOfBytesRead = 0;
        if (lpOverlapped) {
            lpOverlapped->Internal     = 0;
            lpOverlapped->InternalHigh = 0;
            SetEvent(hFile);
            if (lpOverlapped->hEvent) SetEvent(lpOverlapped->hEvent);
        }
    }
    return TRUE;
}

// =============================================================================
// HidD_SetOutputReport — portal OUT commands
// =============================================================================
BOOL WINAPI HookManager::Hooked_HidD_SetOutputReport(
    HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength)
{
    if (!IsOurHandle(HidDeviceObject))
        return oHidD_SetOutputReport(HidDeviceObject, ReportBuffer, ReportBufferLength);

    if (ReportBufferLength > 0) {
        const uint8_t* buffer = static_cast<const uint8_t*>(ReportBuffer);
        // If the first byte is the report ID (0), skip it
        if (buffer[0] == 0 && ReportBufferLength > 1) {
            PortalEmulator::Get().ProcessCommand(buffer + 1);
        } else {
            PortalEmulator::Get().ProcessCommand(buffer);
        }
    }

    return TRUE;
}

// =============================================================================
// HidD_GetAttributes — spoof VID/PID for probe
// =============================================================================
BOOL WINAPI HookManager::Hooked_HidD_GetAttributes(
    HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes)
{
    if (!IsOurHandle(HidDeviceObject))
        return oHidD_GetAttributes(HidDeviceObject, Attributes);

    Attributes->Size          = sizeof(HIDD_ATTRIBUTES);
    Attributes->VendorID      = 0x1430;
    Attributes->ProductID     = 0x0150;
    Attributes->VersionNumber = 0x0001;
    LOG_INFO("HidD_GetAttributes: VID/PID spoofed");
    return TRUE;
}

// =============================================================================
// CloseHandle — only intercept handles WE created
// =============================================================================
BOOL WINAPI HookManager::Hooked_CloseHandle(HANDLE hObject) {
    if (!IsOurHandle(hObject))
        return oCloseHandle(hObject);

    LOG_INFO("CloseHandle: virtual portal handle released, handle=" +
             std::to_string((ULONG_PTR)hObject));
    ReleaseVirtualHandle(hObject);
    // Mark session closed so the next enumeration pass can inject again.
    // Do NOT reset g_injectedThisPass here — that flag is per-enumeration-pass
    // and is only reset by SetupDiGetClassDevsW.
    g_sessionOpen = false;
    return TRUE;
}
