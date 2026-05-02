// ---------------------------------------------------------------------------
//  dinput8.dll proxy
//
//  SnowRunner imports DINPUT8.dll. We drop our DLL into the game folder,
//  Windows loads it first, then we:
//    1. LoadLibrary the *real* system dinput8.dll,
//    2. resolve the handful of exports the game uses,
//    3. load SnowMap.asi (the actual mod) from the game folder,
//    4. forward every imported call to the real DLL.
//
//  Every exported symbol is a thin thunk. Because DirectInput8Create uses
//  the stdcall convention under x86, we preserve the original signatures
//  even though on x64 everything is __fastcall — the call patches through
//  a raw function pointer so it just works.
// ---------------------------------------------------------------------------

#include <windows.h>
#include <shlwapi.h>
#include <cstdio>

namespace {

HMODULE g_realDInput8 = nullptr;
HMODULE g_asiModule   = nullptr;

// Resolved real-DLL function pointers. Populated lazily; nullptr until the
// first proxy call or until DllMain attach, whichever happens first.
FARPROC p_DirectInput8Create   = nullptr;
FARPROC p_DllCanUnloadNow      = nullptr;
FARPROC p_DllGetClassObject    = nullptr;
FARPROC p_DllRegisterServer    = nullptr;
FARPROC p_DllUnregisterServer  = nullptr;
FARPROC p_GetdfDIJoystick      = nullptr;

// Build a path to %SYSTEM32%\dinput8.dll so we never recurse into ourselves.
bool LoadRealDInput8()
{
    if (g_realDInput8) return true;

    wchar_t sysPath[MAX_PATH];
    UINT n = GetSystemDirectoryW(sysPath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    wchar_t dllPath[MAX_PATH];
    if (swprintf_s(dllPath, L"%s\\dinput8.dll", sysPath) < 0) return false;

    g_realDInput8 = LoadLibraryW(dllPath);
    if (!g_realDInput8) return false;

    p_DirectInput8Create  = GetProcAddress(g_realDInput8, "DirectInput8Create");
    p_DllCanUnloadNow     = GetProcAddress(g_realDInput8, "DllCanUnloadNow");
    p_DllGetClassObject   = GetProcAddress(g_realDInput8, "DllGetClassObject");
    p_DllRegisterServer   = GetProcAddress(g_realDInput8, "DllRegisterServer");
    p_DllUnregisterServer = GetProcAddress(g_realDInput8, "DllUnregisterServer");
    p_GetdfDIJoystick     = GetProcAddress(g_realDInput8, "GetdfDIJoystick");
    return true;
}

// Load SnowMap.asi from the directory the host exe lives in.
// We intentionally do this on DLL_PROCESS_ATTACH so the ASI's own DllMain
// runs while the game is still in its startup path — before it creates
// its D3D11 device.
void LoadSnowMapAsi()
{
    if (g_asiModule) return;

    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;
    PathRemoveFileSpecW(exePath);

    wchar_t asiPath[MAX_PATH];
    if (swprintf_s(asiPath, L"%s\\SnowMap.asi", exePath) < 0) return;

    // LOAD_WITH_ALTERED_SEARCH_PATH so the ASI can locate its own deps
    // (e.g. a third-party msvcp runtime sitting next to it).
    g_asiModule = LoadLibraryExW(asiPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    // Intentionally swallow failure — the proxy must still forward input even
    // if the mod is missing, otherwise we'd brick the game.
}

} // namespace

// ---------------------------------------------------------------------------
//  DllMain
// ---------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hInst);
        LoadRealDInput8();
        LoadSnowMapAsi();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        // Windows is tearing down the process; no need to FreeLibrary.
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
//  Forwarded exports.
//
//  Each thunk is a naked-ish forwarder. We cannot use `__declspec(naked)`
//  under x64-MSVC, so we fall back to raw function-pointer casts. The
//  signatures below match Microsoft's DINPUT8 headers exactly.
// ---------------------------------------------------------------------------

using FN_DirectInput8Create = HRESULT (WINAPI*)(
    HINSTANCE, DWORD, REFIID, LPVOID*, void*);

extern "C" __declspec(dllexport) HRESULT WINAPI Proxy_DirectInput8Create(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, void* punkOuter)
{
    if (!p_DirectInput8Create && !LoadRealDInput8()) return E_FAIL;
    return reinterpret_cast<FN_DirectInput8Create>(p_DirectInput8Create)(
        hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

using FN_DllCanUnloadNow     = HRESULT (WINAPI*)();
using FN_DllGetClassObject   = HRESULT (WINAPI*)(REFCLSID, REFIID, LPVOID*);
using FN_DllRegisterServer   = HRESULT (WINAPI*)();
using FN_DllUnregisterServer = HRESULT (WINAPI*)();
using FN_GetdfDIJoystick     = LPVOID  (WINAPI*)();

extern "C" __declspec(dllexport) HRESULT WINAPI Proxy_DllCanUnloadNow()
{
    if (!p_DllCanUnloadNow && !LoadRealDInput8()) return S_FALSE;
    return reinterpret_cast<FN_DllCanUnloadNow>(p_DllCanUnloadNow)();
}

extern "C" __declspec(dllexport) HRESULT WINAPI Proxy_DllGetClassObject(
    REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    if (!p_DllGetClassObject && !LoadRealDInput8()) return CLASS_E_CLASSNOTAVAILABLE;
    return reinterpret_cast<FN_DllGetClassObject>(p_DllGetClassObject)(rclsid, riid, ppv);
}

extern "C" __declspec(dllexport) HRESULT WINAPI Proxy_DllRegisterServer()
{
    if (!p_DllRegisterServer && !LoadRealDInput8()) return E_FAIL;
    return reinterpret_cast<FN_DllRegisterServer>(p_DllRegisterServer)();
}

extern "C" __declspec(dllexport) HRESULT WINAPI Proxy_DllUnregisterServer()
{
    if (!p_DllUnregisterServer && !LoadRealDInput8()) return E_FAIL;
    return reinterpret_cast<FN_DllUnregisterServer>(p_DllUnregisterServer)();
}

extern "C" __declspec(dllexport) LPVOID WINAPI Proxy_GetdfDIJoystick()
{
    if (!p_GetdfDIJoystick && !LoadRealDInput8()) return nullptr;
    return reinterpret_cast<FN_GetdfDIJoystick>(p_GetdfDIJoystick)();
}
