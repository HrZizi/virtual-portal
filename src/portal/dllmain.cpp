#include <windows.h>
#include "dinput8_proxy.h"
#include "hook_manager.h"
#include "logging.h"
#include "config.h"
#include "figure_manager.h"
#include "graphics_hooks.h"
#include <filesystem>

namespace fs = std::filesystem;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);
        
        // 1. Initialize Logger
        Logger::Initialize("virtual_portal.log");
        LOG_INFO("--- Virtual Portal Emulator Starting ---");

        // 2. Initialize Config
        auto iniConfig = std::make_unique<IniConfigProvider>("portal_config.ini");
        if (iniConfig->Load()) {
            Logger::SetDebugEnabled(iniConfig->GetBool("portal", "debug_log", false));
            Config::SetProvider(std::move(iniConfig));
            LOG_INFO("Configuration loaded from portal_config.ini");
        } else {
            LOG_WARN("Could not load portal_config.ini, using defaults");
            Config::SetProvider(std::make_unique<IniConfigProvider>("portal_config.ini"));
        }

        // 3. Initialize Proxy
        if (!DInput8Proxy::Initialize()) {
            LOG_ERR("Failed to initialize dinput8 proxy");
            return FALSE;
        }

        // 4. Initialize Figure Manager & Load initial figure from config
        std::string initialSky = Config::Get().GetString("figures", "slot_0", "");
        if (!initialSky.empty()) {
            FigureManager::Get().LoadFigure(0, initialSky);
        }

        // 5. Install Hooks
        HookManager::Install();
        GraphicsHooks::Install();
        
        LOG_INFO("DLL_PROCESS_ATTACH completed successfully");
        break;
    }
    case DLL_PROCESS_DETACH:
        LOG_INFO("--- Virtual Portal Emulator Shutting Down ---");
        FigureManager::Get().FlushAll();
        HookManager::Uninstall();
        DInput8Proxy::Shutdown();
        break;
    }
    return TRUE;
}
