#pragma once

#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>

#include <stdexcept>

#pragma comment(lib, "wbemuuid.lib")

namespace COM {

    class COMInitializer {
    public:
        COMInitializer();
        ~COMInitializer();

        bool initializeWMI(IWbemLocator*& pLocator, IWbemServices*& pService);
    };

}