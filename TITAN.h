#pragma once

#include "Container/Services/Services.hpp"
#include "Container/Header/FsCleaner.h"
#include "Container/Header/Mac.h"
#include "Container/Header/Registry.h"
#include "Container/Header/WMI.h"

namespace TitanSpoofer {

    inline void noOut() {
        std::cout.setstate(std::ios_base::failbit);
    }

    inline void reOut() {
        std::cout.clear();
    }

    inline bool runTasks(bool logs) {
        try {
            if (!logs) {
                noOut();
            }

            TsService::__TerminateRoblox(); // Kills RobloxPlayerBeta/RobloxCrashHandler/Bloxstrap/RobloxStudioLauncher/RobloxStudio
            FsCleaner::__RemoveTraces();   // Deletes Roblox-related & Roblox files from checked directories

            MAC::MacSpoofer::run(); // Spoofs MAC adapters
            Registry::RegSpoofer::run(); // Spoofs registry hive values
            WMI::WmiSpoofer::run(); // Spoofs WMI values

            FsCleaner::__ReInstall(); // Reinstalls Roblox (net-less, via Bloxstrap/Fishstrap)

            if (!logs) {
                reOut();
            }

            return true; // SUCCESS
        }
        catch (const std::exception& ex) {
            if (!logs) {
                reOut();
            }
            std::cerr << "TS err -> " << ex.what() << std::endl;
            return false; // FAILURE
        }
    }

    inline std::thread run(bool logs) {
        return std::thread([logs]() {
            runTasks(logs);
            });
    }
}