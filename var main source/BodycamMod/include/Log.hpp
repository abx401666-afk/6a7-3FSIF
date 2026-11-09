#pragma once
#include <string>
#include <vector>

enum LogLevel_ : int { Log_Info = 0, Log_Warn, Log_Error, Log_Kill, Log_Hit, Log_Miss, Log_LevelCount };

struct LogEntry {
    double      time = 0.0;
    std::string clock;
    int         level = Log_Info;
    std::string text;
};

void Log_Add(int level, const char* fmt, ...);
void Log_InstallCrashHandler();
void Log_SetCrashHook(void (*hook)());
