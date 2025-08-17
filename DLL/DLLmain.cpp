#include <Windows.h>
#include <iostream>

#include "../Container/Services/Services.hpp"
#include "../Container/Header/Installer.h"
#include "../Container/Header/TraceCleaner.h"
#include "../Container/Header/Mac.h"
#include "../Container/Header/Registry.h"
#include "../Container/Header/WMI.h"

extern "C" __declspec(dllexport) void RunSpoofer() {
    TsService::TITAN();
    TsService::__TerminateRoblox();


    TraceCleaner::run();
    MAC::MacSpoofer::run();
    Registry::RegSpoofer::run();
    WMI::WmiSpoofer::run();
    Installer::Install();

}

extern "C" __declspec(dllexport) void KillRoblox() {
    TsService::__TerminateRoblox();
}

extern "C" __declspec(dllexport) void SpoofMAC() {
    MAC::MacSpoofer::run();
}

extern "C" __declspec(dllexport) void CleanFS() {
    TraceCleaner::run();
    Installer::Install();
}

extern "C" __declspec(dllexport) void SpoofRegistry() {
    Registry::RegSpoofer::run();
}

extern "C" __declspec(dllexport) void SpoofWMI() {
    WMI::WmiSpoofer::run();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}