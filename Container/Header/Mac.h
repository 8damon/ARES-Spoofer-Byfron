#pragma once

#include "../Header/COM.h"
#include "../Services/Services.hpp"

#include <windows.h>
#include <iphlpapi.h>
#include <comdef.h>
#include <Wbemidl.h>

#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <iostream>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace MAC {

    class MacSpoofer {
    public:
        static void run();

    private:
        static std::wstring GetCurrentSSID();
        static void bounceAdapter(const std::wstring& adapterName);
        static std::vector<std::wstring> getAdapters();
        static std::optional<std::wstring> resAdapter(const std::wstring& adapterName);
        static std::wstring getAdapterRegPath(const std::wstring& adapterGUID);
        static void spoofMac();
        static std::wstring trim(const std::wstring& s);
    };
}
