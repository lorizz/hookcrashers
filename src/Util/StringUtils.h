#pragma once

#include "../stdafx.h"
#include <string>
#include <vector>
#include <cstddef>

namespace HookCrashers {
    namespace Util {
        std::string BytesToHex(const unsigned char* data, size_t size);
        std::string BytesToHex(const std::vector<unsigned char>& data);
        std::vector<unsigned char> StringToBytes(const std::string& str);
        std::string BytesToString(const unsigned char* data, size_t size);
        std::string BytesToString(const std::vector<unsigned char>& data);

    }
}