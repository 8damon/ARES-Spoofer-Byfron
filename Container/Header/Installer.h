#pragma once
#include <string>

namespace TITAN { class Watchdog; }

namespace Installer {
    void Install();                    // dll
    void Install(TITAN::Watchdog& wd); // exe
}