#include "HookManager.h"
#include "DXGIHook.h"
#include "FileSniffer.h"
#include "D3D11Hook.h"
#include "../core/Logger.h"

#include <MinHook.h>

namespace snowmap::hooks {

bool InitHookEngine()
{
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        SM_ERROR("MH_Initialize failed: %d", int(s));
        return false;
    }
    return true;
}

void ShutdownHookEngine()
{
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}

bool InstallStaticHooks()
{
    bool ok = true;
    ok &= InstallDXGIHook();
    ok &= InstallFileSniffer();
    return ok;
}

bool InstallDeviceHooks()
{
    return InstallD3D11Hook();
}

} // namespace snowmap::hooks