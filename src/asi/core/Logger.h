#pragma once
//  Small, thread-safe logger that writes to <gamedir>/SnowMap/SnowMap.log.
//  The game runs without a console, so we cannot rely on stdout.
//  Keep it cheap — no formatting libs, just snprintf.

#include <string>

namespace snowmap {

enum class LogLevel { Info, Warn, Error, Debug };

class Logger
{
public:
    static void Init();                          // creates log file, called once
    static void Shutdown();
    static void Log(LogLevel lvl, const char* fmt, ...);
};

// Convenience macros.
// NOTE: SM_DEBUG conflicts with winuser.h's SM_DEBUG (= 22), so we use SNOWMAP_DBG.
#define SM_INFO(...)     ::snowmap::Logger::Log(::snowmap::LogLevel::Info,  __VA_ARGS__)
#define SM_WARN(...)     ::snowmap::Logger::Log(::snowmap::LogLevel::Warn,  __VA_ARGS__)
#define SM_ERROR(...)    ::snowmap::Logger::Log(::snowmap::LogLevel::Error, __VA_ARGS__)
#define SNOWMAP_DBG(...) ::snowmap::Logger::Log(::snowmap::LogLevel::Debug, __VA_ARGS__)

} // namespace snowmap
