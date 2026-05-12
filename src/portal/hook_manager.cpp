#define DIRECTINPUT_VERSION 0x0800
#include "hook_manager.h"
#include "MinHook.h"
#include "logging.h"
#include "portal_emulator.h"
#include "imgui_manager.h"
#include "imgui.h"
#include <dinput.h>
#include <cstring>
#include <mutex>
#include <set>


HookManager::PSetupDiGetClassDevsW HookManager::oSetupDiGetClassDevsW = nullptr;
HookManager::PSetupDiEnumDeviceInterfaces
    HookManager::oSetupDiEnumDeviceInterfaces = nullptr;
HookManager::PSetupDiGetDeviceInterfaceDetailW
    HookManager::oSetupDiGetDeviceInterfaceDetailW = nullptr;
HookManager::PSetupDiDestroyDeviceInfoList
    HookManager::oSetupDiDestroyDeviceInfoList = nullptr;
HookManager::PCreateFileW HookManager::oCreateFileW = nullptr;
HookManager::PReadFile HookManager::oReadFile = nullptr;
HookManager::PHidD_SetOutputReport HookManager::oHidD_SetOutputReport = nullptr;
HookManager::PHidD_GetAttributes HookManager::oHidD_GetAttributes = nullptr;
HookManager::PCloseHandle HookManager::oCloseHandle = nullptr;
HookManager::PXInputGetState HookManager::oXInputGetState = nullptr;
HookManager::PGetAsyncKeyState HookManager::oGetAsyncKeyState = nullptr;
HookManager::PGetKeyState HookManager::oGetKeyState = nullptr;
HookManager::PGetKeyboardState HookManager::oGetKeyboardState = nullptr;
HookManager::PPeekMessageA HookManager::oPeekMessageA = nullptr;
HookManager::PGetMessageA HookManager::oGetMessageA = nullptr;
HookManager::PJoyGetPosEx HookManager::oJoyGetPosEx = nullptr;

// =============================================================================
// Handle tracking
//
// Using a fixed sentinel handle value caused collisions with real system
// handles, leading to spurious CloseHandle resets. Instead we track every
// handle we fabricate in a set.
// =============================================================================
static std::set<HANDLE> g_ourHandles;
static std::mutex g_handleMutex;
static std::mutex g_inputMutex;
static XINPUT_STATE g_lastXInputState = {};
static bool g_hasXInputState = false;
static JOYINFOEX g_lastJoyInfo = {};
static bool g_hasJoyInfo = false;
struct CapturedDirectInputState {
  LONG x = 0x7FFF;
  LONG y = 0x7FFF;
  DWORD pov = 0xFFFFFFFF;
  BYTE buttons[128] = {};
};
static CapturedDirectInputState g_lastDirectInputState = {};
static bool g_hasDirectInputState = false;
static bool g_xinputToggleWasDown = false;
static bool g_joyToggleWasDown = false;
static bool g_directInputToggleWasDown = false;
static thread_local bool g_forwardingInputToImGui = false;

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
static bool g_sessionOpen =
    false; // true while game holds a real SESSION handle

// Returns true if the wide path belongs to a portal device (matches VID+PID)
static bool IsPortalPath(LPCWSTR path) {
  if (!path)
    return false;
  // Match our injected synthetic path OR any real portal path
  return (wcsstr(path, L"vid_1430") != nullptr &&
          wcsstr(path, L"pid_0150") != nullptr) ||
         (wcsstr(path, L"VID_1430") != nullptr &&
          wcsstr(path, L"PID_0150") != nullptr);
}

static bool ShouldBlockInputMessage(UINT msg, WPARAM wParam) {
  if ((msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN ||
       msg == WM_SYSKEYUP) &&
      wParam == VK_INSERT) {
    return false;
  }

  switch (msg) {
  case WM_INPUT:
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_CHAR:
  case WM_DEADCHAR:
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP:
  case WM_SYSCHAR:
  case WM_SYSDEADCHAR:
  case WM_MOUSEMOVE:
  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_LBUTTONDBLCLK:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
  case WM_RBUTTONDBLCLK:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP:
  case WM_MBUTTONDBLCLK:
  case WM_MOUSEWHEEL:
  case WM_MOUSEHWHEEL:
  case WM_XBUTTONDOWN:
  case WM_XBUTTONUP:
  case WM_XBUTTONDBLCLK:
    return true;
  default:
    return false;
  }
}

static void ClearKeyboardState(PBYTE state) {
  if (state)
    ZeroMemory(state, 256);
}

static void NeutralizeJoyInfoEx(LPJOYINFOEX pji) {
  if (!pji)
    return;

  pji->dwXpos = 0x7FFF;
  pji->dwYpos = 0x7FFF;
  pji->dwZpos = 0x7FFF;
  pji->dwRpos = 0x7FFF;
  pji->dwUpos = 0x7FFF;
  pji->dwVpos = 0x7FFF;
  pji->dwButtons = 0;
  pji->dwButtonNumber = 0;
  pji->dwPOV = JOY_POVCENTERED;
}

static void ForwardBlockedInputToImGui(const MSG &msg) {
  if (!ImGuiManager::Get().IsInitialized())
    return;

  g_forwardingInputToImGui = true;
  ImGuiManager::Get().HandleWndProc(msg.hwnd, msg.message, msg.wParam,
                                    msg.lParam);
  g_forwardingInputToImGui = false;
}

static float Saturate(float value) {
  if (value < 0.0f)
    return 0.0f;
  if (value > 1.0f)
    return 1.0f;
  return value;
}

static void AddGamepadButton(ImGuiIO &io, ImGuiKey key, bool down) {
  io.AddKeyEvent(key, down);
}

static void AddGamepadAnalog(ImGuiIO &io, ImGuiKey key, float value) {
  io.AddKeyAnalogEvent(key, value > 0.10f, Saturate(value));
}

static float XInputAnalog(SHORT value, SHORT deadzone, SHORT edge) {
  float normalized = (float)(value - deadzone) / (float)(edge - deadzone);
  return Saturate(normalized);
}

static float JoyAxisNegative(DWORD value) {
  const float center = 32767.0f;
  const float deadzone = 8000.0f;
  if ((float)value >= center - deadzone)
    return 0.0f;
  return Saturate((center - deadzone - (float)value) / (center - deadzone));
}

static float JoyAxisPositive(DWORD value) {
  const float center = 32767.0f;
  const float deadzone = 8000.0f;
  if ((float)value <= center + deadzone)
    return 0.0f;
  return Saturate(((float)value - center - deadzone) /
                  (65535.0f - center - deadzone));
}

static float DirectInputAxisNegative(LONG value) {
  if (value > 10000)
    return JoyAxisNegative((DWORD)value);
  if (value >= -500)
    return 0.0f;
  return Saturate((float)(-value - 500) / 9500.0f);
}

static float DirectInputAxisPositive(LONG value) {
  if (value > 10000)
    return JoyAxisPositive((DWORD)value);
  if (value <= 500)
    return 0.0f;
  return Saturate((float)(value - 500) / 9500.0f);
}

static bool ButtonDown(BYTE value) {
  return (value & 0x80) != 0;
}

static bool CheckControllerToggle(bool down, bool &wasDown) {
  const bool pressed = down && !wasDown;
  wasDown = down;
  if (pressed && ImGuiManager::Get().IsInitialized())
    ImGuiManager::Get().ToggleVisibility();
  return pressed;
}

// =============================================================================
void HookManager::Install() {
  MH_STATUS status = MH_Initialize();
  if (status != MH_OK) {
    LOG_ERR("MinHook failed to initialize: " + std::to_string(status));
    return;
  }

  auto CreateAndEnable = [](const wchar_t* pszLibName, const char* pszProcName, LPVOID pDetour, LPVOID* ppOriginal, const std::string& name) {
    MH_STATUS s = MH_CreateHookApi(pszLibName, pszProcName, pDetour, ppOriginal);
    if (s == MH_OK) {
        s = MH_EnableHook(MH_ALL_HOOKS);
        if (s == MH_OK) {
            LOG_INFO("Hooked " + name);
            return;
        }
    }
    LOG_ERR("Failed to hook " + name + " (status: " + std::to_string(s) + ")");
  };

  CreateAndEnable(L"setupapi", "SetupDiGetClassDevsW", &Hooked_SetupDiGetClassDevsW, reinterpret_cast<LPVOID *>(&oSetupDiGetClassDevsW), "SetupDiGetClassDevsW");
  CreateAndEnable(L"setupapi", "SetupDiEnumDeviceInterfaces", &Hooked_SetupDiEnumDeviceInterfaces, reinterpret_cast<LPVOID *>(&oSetupDiEnumDeviceInterfaces), "SetupDiEnumDeviceInterfaces");
  CreateAndEnable(L"setupapi", "SetupDiGetDeviceInterfaceDetailW", &Hooked_SetupDiGetDeviceInterfaceDetailW, reinterpret_cast<LPVOID *>(&oSetupDiGetDeviceInterfaceDetailW), "SetupDiGetDeviceInterfaceDetailW");
  CreateAndEnable(L"kernel32", "CreateFileW", &Hooked_CreateFileW, reinterpret_cast<LPVOID *>(&oCreateFileW), "CreateFileW");
  CreateAndEnable(L"kernel32", "ReadFile", &Hooked_ReadFile, reinterpret_cast<LPVOID *>(&oReadFile), "ReadFile");
  CreateAndEnable(L"hid", "HidD_SetOutputReport", &Hooked_HidD_SetOutputReport, reinterpret_cast<LPVOID *>(&oHidD_SetOutputReport), "HidD_SetOutputReport");
  CreateAndEnable(L"hid", "HidD_GetAttributes", &Hooked_HidD_GetAttributes, reinterpret_cast<LPVOID *>(&oHidD_GetAttributes), "HidD_GetAttributes");
  CreateAndEnable(L"kernel32", "CloseHandle", &Hooked_CloseHandle, reinterpret_cast<LPVOID *>(&oCloseHandle), "CloseHandle");

  // --- Input Blackout Hooks ---
  CreateAndEnable(L"user32", "GetAsyncKeyState", &Hooked_GetAsyncKeyState, reinterpret_cast<LPVOID *>(&oGetAsyncKeyState), "GetAsyncKeyState");
  CreateAndEnable(L"user32", "GetKeyState", &Hooked_GetKeyState, reinterpret_cast<LPVOID *>(&oGetKeyState), "GetKeyState");
  CreateAndEnable(L"user32", "GetKeyboardState", &Hooked_GetKeyboardState, reinterpret_cast<LPVOID *>(&oGetKeyboardState), "GetKeyboardState");
  CreateAndEnable(L"user32", "PeekMessageA", &Hooked_PeekMessageA, reinterpret_cast<LPVOID *>(&oPeekMessageA), "PeekMessageA");
  CreateAndEnable(L"user32", "GetMessageA", &Hooked_GetMessageA, reinterpret_cast<LPVOID *>(&oGetMessageA), "GetMessageA");
  CreateAndEnable(L"winmm", "joyGetPosEx", &Hooked_joyGetPosEx, reinterpret_cast<LPVOID *>(&oJoyGetPosEx), "joyGetPosEx");

  // XInput (Scattergun approach)
  const char* xinputDlls[] = { "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll" };
  HMODULE hXInput = nullptr;
  for (const char* dll : xinputDlls) {
      hXInput = GetModuleHandleA(dll);
      if (hXInput) {
          LOG_INFO("Detected " + std::string(dll) + " loaded");
          FARPROC pGetState = GetProcAddress(hXInput, "XInputGetState");
          if (pGetState) {
              MH_STATUS s = MH_CreateHook(pGetState, &Hooked_XInputGetState, reinterpret_cast<LPVOID *>(&oXInputGetState));
              if (s == MH_OK) {
                  s = MH_EnableHook(pGetState);
                  if (s == MH_OK) {
                      LOG_INFO("Hooked XInputGetState in " + std::string(dll));
                      break;
                  }
              }
              LOG_ERR("Failed to hook XInputGetState in " + std::string(dll) + " (status: " + std::to_string(s) + ")");
          }
      }
  }
}

void HookManager::Uninstall() {
  MH_DisableHook(MH_ALL_HOOKS);
  MH_Uninitialize();
  LOG_INFO("Hooks uninstalled");
}

bool HookManager::CaptureDirectInputState(DWORD cbData, const void *data) {
  if (!data)
    return false;

  CapturedDirectInputState captured = {};
  bool hasState = false;

  if (cbData == sizeof(DIJOYSTATE)) {
    const DIJOYSTATE *state = static_cast<const DIJOYSTATE *>(data);
    captured.x = state->lX;
    captured.y = state->lY;
    captured.pov = state->rgdwPOV[0];
    memcpy(captured.buttons, state->rgbButtons, sizeof(state->rgbButtons));
    hasState = true;
  } else if (cbData == sizeof(DIJOYSTATE2)) {
    const DIJOYSTATE2 *state = static_cast<const DIJOYSTATE2 *>(data);
    captured.x = state->lX;
    captured.y = state->lY;
    captured.pov = state->rgdwPOV[0];
    memcpy(captured.buttons, state->rgbButtons, sizeof(state->rgbButtons));
    hasState = true;
  }

  if (!hasState)
    return false;

  bool toggleDown = ButtonDown(captured.buttons[9]) ||
                    ButtonDown(captured.buttons[12]) ||
                    ButtonDown(captured.buttons[13]);
  bool toggledOverlay =
      CheckControllerToggle(toggleDown, g_directInputToggleWasDown);

  {
    std::lock_guard<std::mutex> lk(g_inputMutex);
    g_lastDirectInputState = captured;
    g_hasDirectInputState = true;
  }

  return toggledOverlay;
}

bool HookManager::CaptureDirectInputButtonEvent(DWORD objectOffset,
                                                DWORD data) {
  const bool down = (data & 0x80) != 0;
  const bool isToggleButton =
      objectOffset == DIJOFS_BUTTON(9) || objectOffset == DIJOFS_BUTTON(12) ||
      objectOffset == DIJOFS_BUTTON(13);
  if (!isToggleButton)
    return false;
  return CheckControllerToggle(down, g_directInputToggleWasDown);
}

void HookManager::SubmitGamepadNavigation() {
  if (!ImGuiManager::Get().IsVisible())
    return;

  XINPUT_STATE xinputState = {};
  JOYINFOEX joyInfo = {};
  CapturedDirectInputState directInputState = {};
  bool hasXInput = false;
  bool hasJoy = false;
  bool hasDirectInput = false;

  if (oXInputGetState && oXInputGetState(0, &xinputState) == ERROR_SUCCESS) {
    hasXInput = true;
  } else if (oJoyGetPosEx) {
    joyInfo.dwSize = sizeof(JOYINFOEX);
    joyInfo.dwFlags = JOY_RETURNALL;
    hasJoy = (oJoyGetPosEx(0, &joyInfo) == JOYERR_NOERROR);
  }

  {
    std::lock_guard<std::mutex> lk(g_inputMutex);
    if (hasXInput) {
      g_lastXInputState = xinputState;
      g_hasXInputState = true;
    } else if (g_hasXInputState) {
      xinputState = g_lastXInputState;
      hasXInput = true;
    }

    if (g_hasDirectInputState) {
      directInputState = g_lastDirectInputState;
      hasDirectInput = true;
    }

    if (hasJoy) {
      g_lastJoyInfo = joyInfo;
      g_hasJoyInfo = true;
    } else if (!hasXInput && !hasDirectInput && g_hasJoyInfo) {
      joyInfo = g_lastJoyInfo;
      hasJoy = true;
    }
  }

  ImGuiIO &io = ImGui::GetIO();
  io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  if (!hasXInput && !hasDirectInput && !hasJoy)
    return;

  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

  if (hasXInput) {
    const XINPUT_GAMEPAD &gamepad = xinputState.Gamepad;
    const WORD buttons = gamepad.wButtons;
    const bool stickLeft =
        gamepad.sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    const bool stickRight =
        gamepad.sThumbLX > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    const bool stickUp =
        gamepad.sThumbLY > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    const bool stickDown =
        gamepad.sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    AddGamepadButton(io, ImGuiKey_GamepadStart,
                     (buttons & XINPUT_GAMEPAD_START) != 0);
    AddGamepadButton(io, ImGuiKey_GamepadBack,
                     (buttons & XINPUT_GAMEPAD_BACK) != 0);
    AddGamepadButton(io, ImGuiKey_GamepadFaceLeft,
                     (buttons & XINPUT_GAMEPAD_X) != 0);
    AddGamepadButton(io, ImGuiKey_GamepadFaceRight,
                     (buttons & XINPUT_GAMEPAD_B) != 0);
    AddGamepadButton(io, ImGuiKey_GamepadFaceUp,
                     (buttons & XINPUT_GAMEPAD_Y) != 0);
    AddGamepadButton(io, ImGuiKey_GamepadFaceDown,
                     (buttons & XINPUT_GAMEPAD_A) != 0);
    AddGamepadButton(io, ImGuiKey_GamepadDpadLeft,
                     (buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0 || stickLeft);
    AddGamepadButton(io, ImGuiKey_GamepadDpadRight,
                     (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0 || stickRight);
    AddGamepadButton(io, ImGuiKey_GamepadDpadUp,
                     (buttons & XINPUT_GAMEPAD_DPAD_UP) != 0 || stickUp);
    AddGamepadButton(io, ImGuiKey_GamepadDpadDown,
                     (buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0 || stickDown);
    AddGamepadButton(io, ImGuiKey_GamepadL1,
                     (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
    AddGamepadButton(io, ImGuiKey_GamepadR1,
                     (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
    AddGamepadButton(io, ImGuiKey_GamepadL3,
                     (buttons & XINPUT_GAMEPAD_LEFT_THUMB) != 0);
    AddGamepadButton(io, ImGuiKey_GamepadR3,
                     (buttons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
    AddGamepadAnalog(
        io, ImGuiKey_GamepadL2,
        ((float)gamepad.bLeftTrigger - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
            (255.0f - XINPUT_GAMEPAD_TRIGGER_THRESHOLD));
    AddGamepadAnalog(
        io, ImGuiKey_GamepadR2,
        ((float)gamepad.bRightTrigger - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
            (255.0f - XINPUT_GAMEPAD_TRIGGER_THRESHOLD));
    AddGamepadAnalog(io, ImGuiKey_GamepadLStickLeft,
                     XInputAnalog(gamepad.sThumbLX,
                                  -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE,
                                  -32768));
    AddGamepadAnalog(io, ImGuiKey_GamepadLStickRight,
                     XInputAnalog(gamepad.sThumbLX,
                                  XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, 32767));
    AddGamepadAnalog(io, ImGuiKey_GamepadLStickUp,
                     XInputAnalog(gamepad.sThumbLY,
                                  XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, 32767));
    AddGamepadAnalog(io, ImGuiKey_GamepadLStickDown,
                     XInputAnalog(gamepad.sThumbLY,
                                  -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE,
                                  -32768));
    return;
  }

  if (hasDirectInput) {
    const BYTE *buttons = directInputState.buttons;
    const bool stickLeft = DirectInputAxisNegative(directInputState.x) > 0.35f;
    const bool stickRight = DirectInputAxisPositive(directInputState.x) > 0.35f;
    const bool stickUp = DirectInputAxisNegative(directInputState.y) > 0.35f;
    const bool stickDown = DirectInputAxisPositive(directInputState.y) > 0.35f;

    bool povUp = false;
    bool povRight = false;
    bool povDown = false;
    bool povLeft = false;
    if (directInputState.pov != JOY_POVCENTERED &&
        directInputState.pov != 0xFFFFFFFF) {
      povUp = directInputState.pov >= 31500 || directInputState.pov <= 4500;
      povRight = directInputState.pov >= 4500 && directInputState.pov <= 13500;
      povDown = directInputState.pov >= 13500 && directInputState.pov <= 22500;
      povLeft = directInputState.pov >= 22500 && directInputState.pov <= 31500;
    }

    AddGamepadButton(io, ImGuiKey_GamepadStart,
                     ButtonDown(buttons[9]) || ButtonDown(buttons[12]) ||
                         ButtonDown(buttons[13]));
    AddGamepadButton(io, ImGuiKey_GamepadBack, ButtonDown(buttons[8]));
    AddGamepadButton(io, ImGuiKey_GamepadFaceLeft, ButtonDown(buttons[0]));
    AddGamepadButton(io, ImGuiKey_GamepadFaceDown, ButtonDown(buttons[1]));
    AddGamepadButton(io, ImGuiKey_GamepadFaceRight, ButtonDown(buttons[2]));
    AddGamepadButton(io, ImGuiKey_GamepadFaceUp, ButtonDown(buttons[3]));
    AddGamepadButton(io, ImGuiKey_GamepadL1, ButtonDown(buttons[4]));
    AddGamepadButton(io, ImGuiKey_GamepadR1, ButtonDown(buttons[5]));
    AddGamepadButton(io, ImGuiKey_GamepadL2, ButtonDown(buttons[6]));
    AddGamepadButton(io, ImGuiKey_GamepadR2, ButtonDown(buttons[7]));
    AddGamepadButton(io, ImGuiKey_GamepadL3, ButtonDown(buttons[10]));
    AddGamepadButton(io, ImGuiKey_GamepadR3, ButtonDown(buttons[11]));
    AddGamepadButton(io, ImGuiKey_GamepadDpadUp, povUp || stickUp);
    AddGamepadButton(io, ImGuiKey_GamepadDpadRight, povRight || stickRight);
    AddGamepadButton(io, ImGuiKey_GamepadDpadDown, povDown || stickDown);
    AddGamepadButton(io, ImGuiKey_GamepadDpadLeft, povLeft || stickLeft);
    AddGamepadAnalog(io, ImGuiKey_GamepadLStickLeft,
                     DirectInputAxisNegative(directInputState.x));
    AddGamepadAnalog(io, ImGuiKey_GamepadLStickRight,
                     DirectInputAxisPositive(directInputState.x));
    AddGamepadAnalog(io, ImGuiKey_GamepadLStickUp,
                     DirectInputAxisNegative(directInputState.y));
    AddGamepadAnalog(io, ImGuiKey_GamepadLStickDown,
                     DirectInputAxisPositive(directInputState.y));
    return;
  }

  const DWORD buttons = joyInfo.dwButtons;
  AddGamepadButton(io, ImGuiKey_GamepadFaceLeft, (buttons & JOY_BUTTON1) != 0);
  AddGamepadButton(io, ImGuiKey_GamepadFaceDown, (buttons & JOY_BUTTON2) != 0);
  AddGamepadButton(io, ImGuiKey_GamepadFaceRight, (buttons & JOY_BUTTON3) != 0);
  AddGamepadButton(io, ImGuiKey_GamepadFaceUp, (buttons & JOY_BUTTON4) != 0);
  AddGamepadButton(io, ImGuiKey_GamepadL1, (buttons & JOY_BUTTON5) != 0);
  AddGamepadButton(io, ImGuiKey_GamepadR1, (buttons & JOY_BUTTON6) != 0);
  AddGamepadButton(io, ImGuiKey_GamepadL2, (buttons & JOY_BUTTON7) != 0);
  AddGamepadButton(io, ImGuiKey_GamepadR2, (buttons & JOY_BUTTON8) != 0);
  AddGamepadButton(io, ImGuiKey_GamepadBack, (buttons & JOY_BUTTON9) != 0);
  AddGamepadButton(io, ImGuiKey_GamepadStart,
                   (buttons & JOY_BUTTON10) != 0 ||
                       (buttons & JOY_BUTTON13) != 0 ||
                       (buttons & JOY_BUTTON14) != 0);

  bool povUp = false;
  bool povRight = false;
  bool povDown = false;
  bool povLeft = false;
  if (joyInfo.dwPOV != JOY_POVCENTERED && joyInfo.dwPOV != 0xFFFFFFFF) {
    povUp = joyInfo.dwPOV >= 31500 || joyInfo.dwPOV <= 4500;
    povRight = joyInfo.dwPOV >= 4500 && joyInfo.dwPOV <= 13500;
    povDown = joyInfo.dwPOV >= 13500 && joyInfo.dwPOV <= 22500;
    povLeft = joyInfo.dwPOV >= 22500 && joyInfo.dwPOV <= 31500;
  }

  const bool stickLeft = JoyAxisNegative(joyInfo.dwXpos) > 0.35f;
  const bool stickRight = JoyAxisPositive(joyInfo.dwXpos) > 0.35f;
  const bool stickUp = JoyAxisNegative(joyInfo.dwYpos) > 0.35f;
  const bool stickDown = JoyAxisPositive(joyInfo.dwYpos) > 0.35f;

  AddGamepadButton(io, ImGuiKey_GamepadDpadUp, povUp || stickUp);
  AddGamepadButton(io, ImGuiKey_GamepadDpadRight, povRight || stickRight);
  AddGamepadButton(io, ImGuiKey_GamepadDpadDown, povDown || stickDown);
  AddGamepadButton(io, ImGuiKey_GamepadDpadLeft, povLeft || stickLeft);
  AddGamepadAnalog(io, ImGuiKey_GamepadLStickLeft,
                   JoyAxisNegative(joyInfo.dwXpos));
  AddGamepadAnalog(io, ImGuiKey_GamepadLStickRight,
                   JoyAxisPositive(joyInfo.dwXpos));
  AddGamepadAnalog(io, ImGuiKey_GamepadLStickUp,
                   JoyAxisNegative(joyInfo.dwYpos));
  AddGamepadAnalog(io, ImGuiKey_GamepadLStickDown,
                   JoyAxisPositive(joyInfo.dwYpos));
}

// =============================================================================
// SetupDiGetClassDevsW — new enumeration pass starts; reset injection flag
// =============================================================================
HDEVINFO WINAPI HookManager::Hooked_SetupDiGetClassDevsW(const GUID *ClassGuid,
                                                         PCWSTR Enumerator,
                                                         HWND hwndParent,
                                                         DWORD Flags) {
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
    const GUID *InterfaceClassGuid, DWORD MemberIndex,
    PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData) {
  BOOL result = oSetupDiEnumDeviceInterfaces(DeviceInfoSet, DeviceInfoData,
                                             InterfaceClassGuid, MemberIndex,
                                             DeviceInterfaceData);

  if (!result && GetLastError() == ERROR_NO_MORE_ITEMS && !g_injectedThisPass &&
      !g_sessionOpen) {
    g_injectedThisPass = true;

    ZeroMemory(DeviceInterfaceData, sizeof(SP_DEVICE_INTERFACE_DATA));
    DeviceInterfaceData->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    DeviceInterfaceData->InterfaceClassGuid = *InterfaceClassGuid;
    DeviceInterfaceData->Flags = 0x1337;
    DeviceInterfaceData->Reserved = 0x1337;

    SetLastError(0);
    LOG_INFO("Virtual portal injected at MemberIndex " +
             std::to_string(MemberIndex));
    return TRUE;
  }

  return result;
}

// =============================================================================
// SetupDiGetDeviceInterfaceDetailW — return synthetic path for our sentinel
// =============================================================================
BOOL WINAPI HookManager::Hooked_SetupDiGetDeviceInterfaceDetailW(
    HDEVINFO DeviceInfoSet, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData,
    DWORD DeviceInterfaceDetailDataSize, PDWORD RequiredSize,
    PSP_DEVINFO_DATA DeviceInfoDataOut) {
  if (DeviceInterfaceData && DeviceInterfaceData->Flags == 0x1337) {
    const wchar_t *path = VIRTUAL_PORTAL_PATH_W;
    DWORD pathBytes = (DWORD)((wcslen(path) + 1) * sizeof(wchar_t));
    DWORD needed =
        offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath) + pathBytes;

    if (RequiredSize)
      *RequiredSize = needed;

    if (!DeviceInterfaceDetailData || DeviceInterfaceDetailDataSize < needed) {
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }

    DeviceInterfaceDetailData->cbSize =
        sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    wcscpy_s(DeviceInterfaceDetailData->DevicePath,
             (DeviceInterfaceDetailDataSize -
              offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath)) /
                 sizeof(wchar_t),
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
    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
  // Convert wide path to narrow for logging
  auto wToA = [](LPCWSTR w) -> std::string {
    if (!w)
      return "";
    int n = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w, -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0')
      s.pop_back();
    return s;
  };

  if (lpFileName) {
    std::string narrowPath = wToA(lpFileName);
    // Log any path containing "hid#" to see what the probe is actually opening
    if (narrowPath.find("hid#") != std::string::npos ||
        narrowPath.find("HID#") != std::string::npos) {
      char buf[16];
      sprintf_s(buf, "%08X", dwDesiredAccess);
      LOG_INFO("CreateFileW INTERCEPT access=0x" + std::string(buf) +
               " path=" + narrowPath);
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
      char buf[16];
      sprintf_s(buf, "%08X", dwDesiredAccess);
      LOG_INFO("CreateFileW SESSION access=0x" + std::string(buf) + " path=" +
               wToA(lpFileName) + " handle=" + std::to_string((ULONG_PTR)h));
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
BOOL WINAPI HookManager::Hooked_ReadFile(HANDLE hFile, LPVOID lpBuffer,
                                         DWORD nNumberOfBytesToRead,
                                         LPDWORD lpNumberOfBytesRead,
                                         LPOVERLAPPED lpOverlapped) {
  if (!IsOurHandle(hFile))
    return oReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead,
                     lpOverlapped);

  uint8_t resp[32] = {0};
  if (!PortalEmulator::Get().GetNextResponse(resp)) {
    PortalEmulator::Get().UpdateStatus(); // rate-limited to 50 Hz internally
    PortalEmulator::Get().GetNextResponse(resp);
  }

  // For HID devices, the first byte of the read buffer is the Report ID
  // (usually 0). The actual payload starts at offset 1.
  if (nNumberOfBytesToRead > 0) {
    static_cast<uint8_t *>(lpBuffer)[0] = 0; // Report ID

    DWORD toCopy = min(nNumberOfBytesToRead - 1, 32u);
    if (toCopy > 0) {
      std::memcpy(static_cast<uint8_t *>(lpBuffer) + 1, resp, toCopy);
    }

    if (lpNumberOfBytesRead)
      *lpNumberOfBytesRead = toCopy + 1;

    if (lpOverlapped) {
      lpOverlapped->Internal = 0; // STATUS_SUCCESS
      lpOverlapped->InternalHigh = toCopy + 1;
      SetEvent(hFile);
      if (lpOverlapped->hEvent)
        SetEvent(lpOverlapped->hEvent);
    }
  } else {
    if (lpNumberOfBytesRead)
      *lpNumberOfBytesRead = 0;
    if (lpOverlapped) {
      lpOverlapped->Internal = 0;
      lpOverlapped->InternalHigh = 0;
      SetEvent(hFile);
      if (lpOverlapped->hEvent)
        SetEvent(lpOverlapped->hEvent);
    }
  }
  return TRUE;
}

// =============================================================================
// HidD_SetOutputReport — portal OUT commands
// =============================================================================
BOOL WINAPI HookManager::Hooked_HidD_SetOutputReport(HANDLE HidDeviceObject,
                                                     PVOID ReportBuffer,
                                                     ULONG ReportBufferLength) {
  if (!IsOurHandle(HidDeviceObject))
    return oHidD_SetOutputReport(HidDeviceObject, ReportBuffer,
                                 ReportBufferLength);

  if (ReportBufferLength > 0) {
    const uint8_t *buffer = static_cast<const uint8_t *>(ReportBuffer);
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
    HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes) {
  if (!IsOurHandle(HidDeviceObject))
    return oHidD_GetAttributes(HidDeviceObject, Attributes);

  Attributes->Size = sizeof(HIDD_ATTRIBUTES);
  Attributes->VendorID = 0x1430;
  Attributes->ProductID = 0x0150;
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

// =============================================================================
// Input Blackout Hooks
// =============================================================================

DWORD WINAPI HookManager::Hooked_XInputGetState(DWORD dwUserIndex,
                                                XINPUT_STATE *pState) {
  DWORD result = oXInputGetState(dwUserIndex, pState);
  bool toggledOverlay = false;
  if (result == ERROR_SUCCESS && pState) {
    std::lock_guard<std::mutex> lk(g_inputMutex);
    g_lastXInputState = *pState;
    g_hasXInputState = true;
    toggledOverlay =
        CheckControllerToggle((pState->Gamepad.wButtons &
                               XINPUT_GAMEPAD_START) != 0,
                              g_xinputToggleWasDown);
  }
  if (result == ERROR_SUCCESS && pState &&
      (ImGuiManager::Get().IsVisible() || toggledOverlay)) {
    // Zero out controller state so the game doesn't react to buttons/sticks
    ZeroMemory(pState, sizeof(XINPUT_STATE));
  }
  return result;
}

SHORT WINAPI HookManager::Hooked_GetAsyncKeyState(int vKey) {
  if (ImGuiManager::Get().IsVisible()) {
    // Stop the game from reading keyboard state while menu is open
    return 0;
  }
  return oGetAsyncKeyState(vKey);
}

SHORT WINAPI HookManager::Hooked_GetKeyState(int vKey) {
  if (g_forwardingInputToImGui)
    return oGetKeyState(vKey);
  if (ImGuiManager::Get().IsVisible())
    return 0;
  return oGetKeyState(vKey);
}

BOOL WINAPI HookManager::Hooked_GetKeyboardState(PBYTE lpKeyState) {
  BOOL result = oGetKeyboardState(lpKeyState);
  if (g_forwardingInputToImGui)
    return result;
  if (result && ImGuiManager::Get().IsVisible())
    ClearKeyboardState(lpKeyState);
  return result;
}

BOOL WINAPI HookManager::Hooked_PeekMessageA(LPMSG lpMsg, HWND hWnd,
                                             UINT wMsgFilterMin,
                                             UINT wMsgFilterMax,
                                             UINT wRemoveMsg) {
  if (!ImGuiManager::Get().IsVisible()) {
    return oPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax,
                         wRemoveMsg);
  }

  MSG msg;
  LPMSG target = lpMsg ? lpMsg : &msg;
  const bool removeRequested = (wRemoveMsg & PM_REMOVE) == PM_REMOVE;

  while (oPeekMessageA(target, hWnd, wMsgFilterMin, wMsgFilterMax,
                       wRemoveMsg)) {
    if (!ShouldBlockInputMessage(target->message, target->wParam))
      return TRUE;

    ForwardBlockedInputToImGui(*target);

    if (!removeRequested) {
      MSG removed;
      oPeekMessageA(&removed, hWnd, wMsgFilterMin, wMsgFilterMax,
                    PM_REMOVE | (wRemoveMsg & PM_NOYIELD));
    }
  }

  if (lpMsg)
    ZeroMemory(lpMsg, sizeof(MSG));
  return FALSE;
}

BOOL WINAPI HookManager::Hooked_GetMessageA(LPMSG lpMsg, HWND hWnd,
                                            UINT wMsgFilterMin,
                                            UINT wMsgFilterMax) {
  BOOL result;
  do {
    result = oGetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    if (result > 0 && lpMsg && ImGuiManager::Get().IsVisible() &&
        ShouldBlockInputMessage(lpMsg->message, lpMsg->wParam)) {
      ForwardBlockedInputToImGui(*lpMsg);
    }
  } while (result > 0 && lpMsg && ImGuiManager::Get().IsVisible() &&
           ShouldBlockInputMessage(lpMsg->message, lpMsg->wParam));

  return result;
}

MMRESULT WINAPI HookManager::Hooked_joyGetPosEx(UINT uJoyID, LPJOYINFOEX pji) {
  MMRESULT result = oJoyGetPosEx(uJoyID, pji);
  bool toggledOverlay = false;
  if (result == JOYERR_NOERROR && pji) {
    std::lock_guard<std::mutex> lk(g_inputMutex);
    g_lastJoyInfo = *pji;
    g_hasJoyInfo = true;
    toggledOverlay =
        CheckControllerToggle((pji->dwButtons & JOY_BUTTON10) != 0 ||
                                  (pji->dwButtons & JOY_BUTTON13) != 0 ||
                                  (pji->dwButtons & JOY_BUTTON14) != 0,
                              g_joyToggleWasDown);
  }
  if (result == JOYERR_NOERROR &&
      (ImGuiManager::Get().IsVisible() || toggledOverlay))
    NeutralizeJoyInfoEx(pji);
  return result;
}
