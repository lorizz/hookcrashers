#pragma once

#include <string>

namespace HookCrashers::Util {
    class CrashLogger {
    public:
        static void Install(const std::string& logDirectory);
        static void Uninstall();
    };
}