
#pragma once

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <msxml6.h>

#pragma comment(lib, "Wlanapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Uuid.lib")
#pragma comment(lib, "Msxml6.lib")
#pragma comment(lib, "Iphlpapi.lib")

namespace MAC {

    struct MacSpoofer {
        static void run();
    };
}