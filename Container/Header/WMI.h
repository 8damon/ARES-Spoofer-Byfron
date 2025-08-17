#pragma once

#include "../Services/Services.hpp"
#include "../Header/COM.h"

#include <string>
#include <iostream>

namespace WMI {

    class WmiSpoofer {
    public:
        static void run();

    private:
        static bool SystemProduct();
        static bool PhysMem();
    };
}