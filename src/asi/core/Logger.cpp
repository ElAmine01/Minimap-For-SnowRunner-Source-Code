#include "Logger.h"

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h> // Required for FOLDERID_LocalAppData

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace snowmap {
namespace {

std::mutex g_logMutex;
FILE*      g_logFile = nullptr;

const char* LevelTag(LogLevel lvl)
{
    switch (lvl) {
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Debug: return "DEBUG";
    }
    return "?????";
}

// Resolve the path to %LOCALAPPDATA%\SnowMap\SnowMap.log
bool BuildLogPath(char out[MAX_PATH])
{
    PWSTR localAppDataW = NULL;
    // Fetch the %LOCALAPPDATA% path.
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppDataW))) {
        std::wstring ws(localAppDataW);
        CoTaskMemFree(localAppDataW);

        // Build the path: %LOCALAPPDATA%\SnowMap\SnowMap.log
        std::wstring dirW = ws + L"\\SnowMap";
        std::wstring fileW = dirW + L"\\SnowMap.log";

        // Ensure the directory exists.
        CreateDirectoryW(dirW.c_str(), NULL);

        // Convert the wide string path to UTF-8.
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, fileW.c_str(), (int)fileW.length(), NULL, 0, NULL, NULL);
        if (size_needed > 0) {
            std::string finalPath(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, fileW.c_str(), (int)fileW.length(), &finalPath[0], size_needed, NULL, NULL);
            
            // Use the size-explicit overload to avoid C2660 on MSVC.
            strcpy_s(out, MAX_PATH, finalPath.c_str());
            return true;
        }
    }
    return false;
}

} // namespace

void Logger::Init()
{
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) return;

    char path[MAX_PATH];
    if (!BuildLogPath(path)) {
        return;
    }

    // Open the log file in write mode.
    g_logFile = _fsopen(path, "w", _SH_DENYWR);
    if (g_logFile) {
        std::setvbuf(g_logFile, nullptr, _IOLBF, 4096);
    }
}

void Logger::Shutdown()
{
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) {
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
}

void Logger::Log(LogLevel lvl, const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_logFile) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    std::fprintf(g_logFile, "[%02u:%02u:%02u.%03u] [%s] ",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                 LevelTag(lvl));

    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_logFile, fmt, ap);
    va_end(ap);

    std::fputc('\n', g_logFile);
    fflush(g_logFile); 
}

} // namespace snowmap