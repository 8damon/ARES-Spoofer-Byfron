#pragma once

#include "../Services/Services.hpp"
#include "../Services/Defs.h"

#include <string>
#include <vector>
#include <optional>
#include <random>
#include <algorithm>
#include <iostream>

namespace Registry {

    class RegSpoofer {
    public:
        static void run();

    private:
        static bool GUID();
        static bool Users();
     // static bool HardwareInfo();
        static bool EDID();
        static bool Binaries(HANDLE hKey, const std::wstring& valueName, const BYTE* data, size_t dataSize);
    };

}