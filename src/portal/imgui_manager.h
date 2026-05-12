#pragma once
#include <cstdint>
#include <d3d9.h>
#include <string>
#include <windows.h>

// ---------------------------------------------------------------------------
// ImGui Manager — Manages Dear ImGui lifecycle and renders the
// "Virtual Portal Manager" overlay window with card-based slot management.
//
// Integration points (called from graphics_hooks.cpp):
//   Initialize()  → from Hooked_CreateDevice
//   Render()       → from Hooked_EndScene
//   HandleWndProc()→ from Hooked_WndProc
//   InvalidateDeviceObjects() / CreateDeviceObjects() → from Hooked_Reset
// ---------------------------------------------------------------------------

class ImGuiManager {
public:
  static ImGuiManager &Get();

  // Called once after the D3D9 device is created
  void Initialize(IDirect3DDevice9 *pDevice, HWND hWnd);

  // Called every frame from EndScene
  void Render(IDirect3DDevice9 *pDevice);

  // Called before/after Device::Reset
  void InvalidateDeviceObjects();
  void CreateDeviceObjects();

  // Returns true if ImGui consumed the input event
  bool HandleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  void ToggleVisibility();

  bool IsVisible() const { return m_visible; }
  bool IsInitialized() const { return m_initialized; }

private:
  ImGuiManager() = default;

  bool m_initialized = false;
  bool m_visible = false; // toggled by VK_INSERT
  bool m_focusPortalWindow = false;
  float m_uiScale = 1.0f; // DPI scale based on 1080p height

  // UI slot state — maps to FigureManager slots 0, 1, 2
  struct UISlot {
    int fmSlot;        // FigureManager slot index
    const char *label; // "Slot 1", "Slot 2", "Item"
    bool selectorOpen; // is the character selector popup open?
  };
  UISlot m_uiSlots[3];

  // Drawing methods
  void DrawPortalManager();
  void DrawSlotCard(UISlot &slot, bool setDefaultFocus = false);
  void DrawCharacterSelector(UISlot &slot);
};
