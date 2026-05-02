#define NOMINMAX
#include <windows.h>
#include <thread>

#include "core/Logger.h"
#include "core/Config.h"
#include "core/Globals.h"
#include "hooks/HookManager.h"

using namespace snowmap;

DWORD WINAPI ModAttach(LPVOID) {
    Logger::Init();
    SM_INFO("SnowMap.asi loaded (Mode Optimise)");

    char path[MAX_PATH];
    if (Config::ResolveDefaultPath(path, MAX_PATH)) {
        GetConfig().LoadFromFile(path);
        SM_INFO("Config loaded from '%s'.", path);
    } else {
        SM_WARN("Could not resolve config path.");
    }

    if (!hooks::InitHookEngine()) {
        SM_ERROR("Failed to initialize hook engine.");
        return 0;
    }

    if (!hooks::InstallStaticHooks()) {
        SM_ERROR("Failed to install static hooks.");
        return 0;
    }

    SM_INFO("Init complete.");
    return 0;
}

void ModDetach() {
    hooks::ShutdownHookEngine();
    Logger::Shutdown();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, ModAttach, nullptr, 0, nullptr);
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        ModDetach();
    }
    return TRUE;
}