#include "../Header/Registry.h"

namespace Registry {

#define INIT_UNICODE_STRING(uStr, str) RtlInitUnicodeString(&(uStr), (str).c_str())
#define INIT_OBJECT_ATTRIBUTES(objAttr, uStr) InitializeObjectAttributes(&(objAttr), &(uStr), OBJ_CASE_INSENSITIVE, nullptr, nullptr)

    void RegSpoofer::run() {
        TsService::SectHeader("Registry Spoofing", 27);

        if (!GUID()) {
            std::cerr << "Failed to spoof MachineGUID." << std::endl;
        }

        if (!Users()) {
            std::cerr << "Failed to spoof user info." << std::endl;
        }

        /*
        if (!HardwareInfo()) {
            std::cerr << "Failed to spoof hardware info." << std::endl;
        }
        */

        if (!EDID()) {
            std::cerr << "Failed to spoof EDID." << std::endl;
        }
    }

    bool RegSpoofer::Binaries(HANDLE hKey, const std::wstring& valueName, const BYTE* data, size_t dataSize) {
        if (!hKey || dataSize == 0) {
            return false;
        }

        UNICODE_STRING uValueName;
        RtlInitUnicodeString(&uValueName, valueName.c_str());

        auto ntSetValueKey = TsService::GetNtSetValueKey();
        if (!ntSetValueKey) {
            return false;
        }

        NTSTATUS status = ntSetValueKey(hKey, &uValueName, 0, REG_BINARY, data, static_cast<ULONG>(dataSize));
        return NT_SUCCESS(status);
    }

    bool RegSpoofer::GUID() {
        const std::wstring regPath = L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Cryptography";
        UNICODE_STRING uStr;
        OBJECT_ATTRIBUTES objAttr;

        INIT_UNICODE_STRING(uStr, regPath);
        INIT_OBJECT_ATTRIBUTES(objAttr, uStr);

        HANDLE hKey = nullptr;
        auto ntOpenKey = TsService::SeOpenKey();
        if (!ntOpenKey || ntOpenKey(&hKey, KEY_SET_VALUE, &objAttr) != STATUS_SUCCESS) {
            std::wcerr << L"Failed to open MachineGUID key (error: " << GetLastError() << L")." << std::endl;
            return false;
        }

        std::wstring newGUID = TsService::stringToWString(TsService::genGUID());
        UNICODE_STRING valueName;
        RtlInitUnicodeString(&valueName, L"MachineGuid");

        auto ntSetValueKey = TsService::GetNtSetValueKey();
        if (!ntSetValueKey ||
            ntSetValueKey(hKey, &valueName, 0, REG_SZ, newGUID.c_str(),
                static_cast<ULONG>((newGUID.size() + 1) * sizeof(wchar_t))) != STATUS_SUCCESS) {
            std::wcerr << L"Failed to set MachineGUID value (error: " << GetLastError() << L")." << std::endl;
            TsService::SeClose()(hKey);
            return false;
        }

        std::wcout << L"Spoofed -> MachineGUID, new GUID -> " << newGUID << std::endl;
        TsService::SeClose()(hKey);
        return true;
    }

    bool RegSpoofer::Users() {
        std::wstring spoofedUser = TsService::genUsers();

        const std::vector<std::tuple<std::wstring, std::wstring>> userInfo = {
            {L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion", L"RegisteredOwner"},
            {L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion", L"LastLoggedOnUser"},
            {L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", L"DisplayName"}
        };

        for (const auto& [regPath, valueName] : userInfo) {
            UNICODE_STRING uStr, valueNameStr;
            OBJECT_ATTRIBUTES objAttr;

            INIT_UNICODE_STRING(uStr, regPath);
            INIT_OBJECT_ATTRIBUTES(objAttr, uStr);

            HANDLE hKey = nullptr;
            auto ntOpenKey = TsService::SeOpenKey();
            if (!ntOpenKey || ntOpenKey(&hKey, KEY_SET_VALUE, &objAttr) != STATUS_SUCCESS) {
                std::wcerr << L"Failed to open key: " << regPath << L" (error: " << GetLastError() << L")." << std::endl;
                return false;
            }

            INIT_UNICODE_STRING(valueNameStr, valueName);

            auto ntSetValueKey = TsService::GetNtSetValueKey();
            if (!ntSetValueKey ||
                ntSetValueKey(hKey, &valueNameStr, 0, REG_SZ, spoofedUser.c_str(),
                    static_cast<ULONG>((spoofedUser.size() + 1) * sizeof(wchar_t))) != STATUS_SUCCESS) {
                std::wcerr << L"Failed to set value: " << valueName << L" (error: " << GetLastError() << L")." << std::endl;
                TsService::SeClose()(hKey);
                return false;
            }

            TsService::SeClose()(hKey);
            std::wcout << L"Spoofed -> " << valueName << L". New value -> " << spoofedUser << std::endl;
        }

        return true;
    }


    /*
    * SYNZ & WAVE refuse to update their HWID authentication systems (even tho they conflict with Hyperion's) so this'll have to go
    /

    bool RegSpoofer::HardwareInfo() {
        const std::vector<std::tuple<std::wstring, std::wstring, std::wstring>> hardwareInfo = {
            {L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS", L"BaseBoardManufacturer", TsService::genBaseBoardManufacturer()},
            {L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemManufacturer", TsService::genSystemManufacturer()},
            {L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS", L"BIOSVersion", TsService::genBIOSVersion()},
            {L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS", L"BIOSReleaseDate", TsService::genBIOSReleaseDate()}
        };

        for (const auto& [regPath, valueName, newValue] : hardwareInfo) {
            UNICODE_STRING uStr, valueNameStr;
            OBJECT_ATTRIBUTES objAttr;

            INIT_UNICODE_STRING(uStr, regPath);
            INIT_OBJECT_ATTRIBUTES(objAttr, uStr);

            HANDLE hKey = nullptr;
            auto ntOpenKey = TsService::SeOpenKey();
            if (!ntOpenKey || ntOpenKey(&hKey, KEY_SET_VALUE, &objAttr) != STATUS_SUCCESS) {
                std::wcerr << L"Failed to open key: " << regPath << L" (error: " << GetLastError() << L")." << std::endl;
                return false;
            }

            INIT_UNICODE_STRING(valueNameStr, valueName);

            auto ntSetValueKey = TsService::GetNtSetValueKey();
            if (!ntSetValueKey ||
                ntSetValueKey(hKey, &valueNameStr, 0, REG_SZ, newValue.c_str(),
                    static_cast<ULONG>((newValue.size() + 1) * sizeof(wchar_t))) != STATUS_SUCCESS) {
                std::wcerr << L"Failed to set value: " << valueName << L" (error: " << GetLastError() << L")." << std::endl;
                TsService::SeClose()(hKey);
                return false;
            }

            TsService::SeClose()(hKey);
            std::wcout << L"Spoofed -> " << valueName << L". New value -> " << newValue << std::endl;
        }

        return true;
    }
    */

    bool RegSpoofer::EDID() {
        const std::wstring displayKeyPath = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY";

        HKEY hDisplayKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, displayKeyPath.c_str(), 0, KEY_READ | KEY_WRITE, &hDisplayKey) != ERROR_SUCCESS) {
            std::wcerr << L"Failed to open DISPLAY registry key." << std::endl;
            return false;
        }

        DWORD subKeyCount = 0;
        if (RegQueryInfoKeyW(hDisplayKey, nullptr, nullptr, nullptr, &subKeyCount, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            std::wcerr << L"Failed to query DISPLAY key info." << std::endl;
            RegCloseKey(hDisplayKey);
            return false;
        }

        bool spoofed = false;

        for (DWORD i = 0; i < subKeyCount; ++i) {
            WCHAR subKeyName[256];
            DWORD subKeyNameLen = sizeof(subKeyName) / sizeof(WCHAR);

            if (RegEnumKeyExW(hDisplayKey, i, subKeyName, &subKeyNameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                continue;
            }

            std::wstring subKeyPath = displayKeyPath + L"\\" + subKeyName;
            HKEY hDeviceKey = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKeyPath.c_str(), 0, KEY_READ | KEY_WRITE, &hDeviceKey) != ERROR_SUCCESS) {
                continue;
            }

            DWORD deviceSubKeyCount = 0;
            if (RegQueryInfoKeyW(hDeviceKey, nullptr, nullptr, nullptr, &deviceSubKeyCount, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                RegCloseKey(hDeviceKey);
                continue;
            }

            for (DWORD j = 0; j < deviceSubKeyCount; ++j) {
                WCHAR deviceSubKeyName[256];
                DWORD deviceSubKeyNameLen = sizeof(deviceSubKeyName) / sizeof(WCHAR);

                if (RegEnumKeyExW(hDeviceKey, j, deviceSubKeyName, &deviceSubKeyNameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                    continue;
                }

                std::wstring deviceSubKeyPath = subKeyPath + L"\\" + deviceSubKeyName;
                std::vector<std::wstring> edidPaths = {
                    deviceSubKeyPath + L"\\Device Parameters",
                    deviceSubKeyPath + L"\\Control\\Device Parameters",
                    deviceSubKeyPath + L"\\Monitor\\Device Parameters"
                };

                for (const auto& edidPath : edidPaths) {
                    HKEY hEDIDKey = nullptr;
                    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, edidPath.c_str(), 0, KEY_READ | KEY_WRITE, &hEDIDKey) == ERROR_SUCCESS) {
                        BYTE spoofedEDID[128];
                        std::generate(std::begin(spoofedEDID), std::end(spoofedEDID), [] { return static_cast<BYTE>(rand() % 256); });

                        if (RegSetValueExW(hEDIDKey, L"EDID", 0, REG_BINARY, spoofedEDID, sizeof(spoofedEDID)) == ERROR_SUCCESS) {
                            std::wstring id = deviceSubKeyPath.substr(deviceSubKeyPath.find_last_of(L'\\') + 1);

                            std::wstring newID = TsService::genEDID();

                            std::wcout << L"Spoofed -> EDID:" << id << L", New ID -> " << newID << std::endl;
                            spoofed = true;
                        }
                        else {
                            std::wcerr << L"Failed to set EDID value @ " << edidPath << std::endl;
                        }

                        RegCloseKey(hEDIDKey);
                    }
                }
            }

            RegCloseKey(hDeviceKey);
        }

        RegCloseKey(hDisplayKey);

        if (!spoofed) {
            std::wcerr << L"No EDIDs were spoofed" << std::endl;
        }

        return spoofed;
    }
}