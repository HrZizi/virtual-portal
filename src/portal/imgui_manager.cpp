#include "imgui_manager.h"
#include "figure_manager.h"
#include "logging.h"
#include "portal_state.h"
#include "skylander_defs.h"

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

// Forward declare the Win32 ImGui WndProc handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

// ---------------------------------------------------------------------------
// Element colors for UI cards (ABGR format for ImGui)
// ---------------------------------------------------------------------------
static ImVec4 GetElementColor(const std::string &element) {
  if (element == "Air")
    return ImVec4(0.55f, 0.82f, 1.00f, 1.0f); // sky blue
  if (element == "Earth")
    return ImVec4(0.60f, 0.40f, 0.20f, 1.0f); // brown
  if (element == "Fire")
    return ImVec4(1.00f, 0.35f, 0.15f, 1.0f); // orange-red
  if (element == "Water")
    return ImVec4(0.20f, 0.50f, 1.00f, 1.0f); // blue
  if (element == "Magic")
    return ImVec4(0.65f, 0.30f, 0.90f, 1.0f); // purple
  if (element == "Tech")
    return ImVec4(0.90f, 0.60f, 0.10f, 1.0f); // amber
  if (element == "Life")
    return ImVec4(0.25f, 0.80f, 0.25f, 1.0f); // green
  if (element == "Undead")
    return ImVec4(0.45f, 0.45f, 0.55f, 1.0f); // dark grey-blue
  return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
}

static const char *GetElementEmoji(const char *element) {
  if (strcmp(element, "Air") == 0)
    return "[Air]";
  if (strcmp(element, "Earth") == 0)
    return "[Earth]";
  if (strcmp(element, "Fire") == 0)
    return "[Fire]";
  if (strcmp(element, "Water") == 0)
    return "[Water]";
  if (strcmp(element, "Magic") == 0)
    return "[Magic]";
  if (strcmp(element, "Tech") == 0)
    return "[Tech]";
  if (strcmp(element, "Life") == 0)
    return "[Life]";
  if (strcmp(element, "Undead") == 0)
    return "[Undead]";
  return "[?]";
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
ImGuiManager &ImGuiManager::Get() {
  static ImGuiManager instance;
  return instance;
}

// ---------------------------------------------------------------------------
// Initialize — Called once from Hooked_CreateDevice
// ---------------------------------------------------------------------------
void ImGuiManager::Initialize(IDirect3DDevice9 *pDevice, HWND hWnd) {
  if (m_initialized)
    return;

  // Initialize UI slot mappings
  m_uiSlots[0] = {0, "Slot 1 - Character", false};
  m_uiSlots[1] = {1, "Slot 2 - Character", false};
  m_uiSlots[2] = {2, "Item Slot", false};

  // Compute DPI scale: baseline is 1080p
  RECT clientRect;
  GetClientRect(hWnd, &clientRect);
  float screenHeight = (float)(clientRect.bottom - clientRect.top);
  m_uiScale = screenHeight / 1080.0f;
  if (m_uiScale < 1.0f)
    m_uiScale = 1.0f; // Never scale down

  // Create ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // Load font at scaled size
  ImFontConfig fontCfg;
  fontCfg.SizePixels = 16.0f * m_uiScale;
  io.Fonts->AddFontDefault(&fontCfg);

  // Dark theme with custom accent colors
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.FrameRounding = 4.0f;
  style.PopupRounding = 6.0f;
  style.GrabRounding = 4.0f;
  style.ScrollbarRounding = 6.0f;
  style.WindowPadding = ImVec2(12, 12);
  style.FramePadding = ImVec2(8, 4);
  style.ItemSpacing = ImVec2(10, 8);
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.Alpha = 0.96f;

  // Scale style
  style.ScaleAllSizes(m_uiScale);

  // Purple accent (matching Skylanders Magic element)
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.10f, 0.25f, 1.0f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 0.15f, 0.45f, 1.0f);
  style.Colors[ImGuiCol_Header] = ImVec4(0.30f, 0.20f, 0.50f, 0.6f);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.40f, 0.25f, 0.60f, 0.8f);
  style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.30f, 0.65f, 1.0f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.30f, 0.20f, 0.50f, 0.7f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.30f, 0.65f, 0.9f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.35f, 0.75f, 1.0f);
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.06f, 0.12f, 0.94f);
  style.Colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.08f, 0.15f, 0.96f);
  style.Colors[ImGuiCol_Border] = ImVec4(0.35f, 0.25f, 0.55f, 0.5f);

  // Initialize platform/renderer backends
  ImGui_ImplWin32_Init(hWnd);
  ImGui_ImplDX9_Init(pDevice);

  m_initialized = true;
  LOG_INFO("ImGui Manager initialized with scale " + std::to_string(m_uiScale));
}

// ---------------------------------------------------------------------------
// Render — Called every frame from EndScene
// ---------------------------------------------------------------------------
void ImGuiManager::Render(IDirect3DDevice9 *pDevice) {
  if (!m_initialized)
    return;

  // Toggle software cursor based on overlay visibility
  ImGuiIO &io = ImGui::GetIO();
  io.MouseDrawCursor = m_visible;

  if (!m_visible)
    return;

  ImGui_ImplDX9_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  DrawPortalManager();

  ImGui::EndFrame();
  ImGui::Render();
  ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

// ---------------------------------------------------------------------------
// Device object management for Reset
// ---------------------------------------------------------------------------
void ImGuiManager::InvalidateDeviceObjects() {
  if (!m_initialized)
    return;
  ImGui_ImplDX9_InvalidateDeviceObjects();
}

void ImGuiManager::CreateDeviceObjects() {
  if (!m_initialized)
    return;
  ImGui_ImplDX9_CreateDeviceObjects();
}

// ---------------------------------------------------------------------------
// HandleWndProc — Process input, toggle visibility with INSERT
// ---------------------------------------------------------------------------
bool ImGuiManager::HandleWndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam) {
  if (!m_initialized)
    return false;

  // Toggle overlay with VK_INSERT
  if (msg == WM_KEYDOWN && wParam == VK_INSERT) {
    m_visible = !m_visible;
    LOG_INFO(std::string("ImGui overlay ") + (m_visible ? "shown" : "hidden"));
    return true;
  }

  // When visible, let ImGui process input
  if (m_visible) {
    ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

    // Block ALL input from reaching the game while overlay is open
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)
      return true;
    if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)
      return true;
    if (msg == WM_INPUT)
      return true;
    
    // Also block system commands like resizing if we want
    if (msg == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_KEYMENU)
        return true;
  }

  return false;
}

// ---------------------------------------------------------------------------
// DrawPortalManager — Main overlay window
// ---------------------------------------------------------------------------
void ImGuiManager::DrawPortalManager() {
  ImGui::SetNextWindowSize(ImVec2(380 * m_uiScale, 0), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(20 * m_uiScale, 20 * m_uiScale),
                          ImGuiCond_FirstUseEver);

  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
  if (!ImGui::Begin("Virtual Portal Manager", &m_visible, flags)) {
    ImGui::End();
    return;
  }

  // Header separator
  ImGui::TextColored(ImVec4(0.65f, 0.45f, 0.90f, 1.0f),
                     "Skylanders: Spyro's Adventure");
  ImGui::Separator();
  ImGui::Spacing();

  // Draw 3 slot cards
  for (int i = 0; i < 3; ++i) {
    ImGui::PushID(i);
    DrawSlotCard(m_uiSlots[i]);
    ImGui::PopID();
    if (i < 2)
      ImGui::Spacing();
  }

  // Status bar
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  int dirtyCount = 0;
  int occupiedCount = 0;
  for (int i = 0; i < 3; ++i) {
    const FigureSlot &slot = FigureManager::Get().GetSlot(m_uiSlots[i].fmSlot);
    if (slot.occupied)
      ++occupiedCount;
    if (slot.dirty)
      ++dirtyCount;
  }

  ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Portal Active");
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::Text("%d figure(s)", occupiedCount);
  if (dirtyCount > 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%d unsaved",
                       dirtyCount);
  }

  ImGui::End();
}

// ---------------------------------------------------------------------------
// DrawSlotCard — Renders a single slot as a styled card
// ---------------------------------------------------------------------------
void ImGuiManager::DrawSlotCard(UISlot &uiSlot) {
  const FigureSlot &slot = FigureManager::Get().GetSlot(uiSlot.fmSlot);

  // Card background color
  ImVec4 borderColor = slot.occupied ? GetElementColor(slot.element)
                                     : ImVec4(0.3f, 0.3f, 0.35f, 0.6f);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.10f, 0.18f, 0.9f));
  ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,
                      slot.occupied ? 2.0f : 1.0f);

  float cardHeight = (slot.occupied ? 80.0f : 55.0f) * m_uiScale;
  ImGui::BeginChild(uiSlot.label, ImVec2(-1, cardHeight),
                    ImGuiChildFlags_Borders);

  // Slot label
  ImGui::TextColored(ImVec4(0.7f, 0.65f, 0.80f, 1.0f), "%s", uiSlot.label);

  bool wantOpenSelector = false;

  if (slot.occupied) {
    // Occupied: show character info
    ImVec4 elemColor = GetElementColor(slot.element);
    ImGui::TextColored(elemColor, "%s %s",
                       GetElementEmoji(slot.element.c_str()),
                       slot.char_name.c_str());

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70 * m_uiScale);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.8f, 0.2f, 0.2f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
    if (ImGui::Button("Remove", ImVec2(70 * m_uiScale, 0))) {
      FigureManager::Get().RemoveFigure(uiSlot.fmSlot);
      PortalState::SaveCurrentState();
      LOG_INFO("Removed figure from UI slot: " + std::string(uiSlot.label));
    }
    ImGui::PopStyleColor(3);

    if (slot.dirty) {
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 0.7f), "(unsaved changes)");
    }
  } else {
    // Empty: clickable area to open selector
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.3f, 0.2f, 0.5f, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.4f, 0.3f, 0.6f, 0.5f));
    if (ImGui::Button("Click to select a character...", ImVec2(-1, 0))) {
      wantOpenSelector = true;
    }
    ImGui::PopStyleColor(3);
  }

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);

  // Open popup at PARENT window level (same scope as BeginPopupModal)
  if (wantOpenSelector) {
    uiSlot.selectorOpen = true;
    ImGui::OpenPopup("Select Character");
  }

  // Character selector modal — always call so ImGui can find the popup
  DrawCharacterSelector(uiSlot);
}

// ---------------------------------------------------------------------------
// DrawCharacterSelector — Modal popup with characters grouped by element
// ---------------------------------------------------------------------------
void ImGuiManager::DrawCharacterSelector(UISlot &uiSlot) {
  ImGui::SetNextWindowSize(ImVec2(420 * m_uiScale, 520 * m_uiScale),
                           ImGuiCond_FirstUseEver);

  if (ImGui::BeginPopupModal("Select Character", &uiSlot.selectorOpen,
                             ImGuiWindowFlags_NoResize)) {
    ImGui::TextColored(ImVec4(0.65f, 0.45f, 0.90f, 1.0f),
                       "Choose a Skylander for %s", uiSlot.label);
    ImGui::Separator();
    ImGui::Spacing();

    // Scrollable content area
    ImGui::BeginChild("CharList",
                      ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);

    for (size_t e = 0; e < SSA_ELEMENT_COUNT; ++e) {
      const char *element = SSA_ELEMENTS[e];
      ImVec4 elemColor = GetElementColor(element);

      ImGui::PushStyleColor(
          ImGuiCol_Header, ImVec4(elemColor.x, elemColor.y, elemColor.z, 0.3f));
      ImGui::PushStyleColor(
          ImGuiCol_HeaderHovered,
          ImVec4(elemColor.x, elemColor.y, elemColor.z, 0.5f));

      // Element group header
      char headerLabel[64];
      snprintf(headerLabel, sizeof(headerLabel), "%s %s",
               GetElementEmoji(element), element);

      bool open =
          ImGui::CollapsingHeader(headerLabel, ImGuiTreeNodeFlags_DefaultOpen);
      ImGui::PopStyleColor(2);

      if (open) {
        ImGui::Indent(12.0f);
        auto chars = GetSkylandersByElement(element);

        int columns = 2;
        for (size_t c = 0; c < chars.size(); ++c) {
          if (c % columns != 0)
            ImGui::SameLine(0, 12);

          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(elemColor.x * 0.3f, elemColor.y * 0.3f,
                                       elemColor.z * 0.3f, 0.6f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                ImVec4(elemColor.x * 0.5f, elemColor.y * 0.5f,
                                       elemColor.z * 0.5f, 0.8f));
          ImGui::PushStyleColor(
              ImGuiCol_ButtonActive,
              ImVec4(elemColor.x, elemColor.y, elemColor.z, 1.0f));

          if (ImGui::Button(chars[c]->name, ImVec2(180 * m_uiScale, 30 * m_uiScale))) {
            // Load character into the slot
            if (FigureManager::Get().LoadCharacter(
                    static_cast<uint8_t>(uiSlot.fmSlot), chars[c]->id)) {
              PortalState::SaveCurrentState();
              LOG_INFO("Selected " + std::string(chars[c]->name) + " for " +
                       std::string(uiSlot.label));
            }
            uiSlot.selectorOpen = false;
            ImGui::CloseCurrentPopup();
          }
          ImGui::PopStyleColor(3);
        }
        ImGui::Unindent(12.0f);
        ImGui::Spacing();
      }
    }
    ImGui::EndChild();

    // Cancel button
    if (ImGui::Button("Cancel", ImVec2(-1, 0))) {
      uiSlot.selectorOpen = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}
