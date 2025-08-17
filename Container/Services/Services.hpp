#pragma once

#pragma warning(push)
#pragma warning(disable : 4244)

#include "Defs.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <shlobj.h>

#include <string>
#include <filesystem>
#include <regex>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <array>

namespace TsService {

    // CONVERSION

    inline std::string toUtf8(const std::wstring& wstr) {
        std::string result;
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (sizeNeeded > 0) {
            result.resize(sizeNeeded - 1);
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], sizeNeeded, nullptr, nullptr);
        }
        return result;
    }

    inline std::wstring stringToWString(const std::string& str) {
        return std::wstring(str.begin(), str.end());
    }


    // GENERATORS

    static thread_local std::mt19937 gen(std::random_device{}());

    inline std::wstring genRand(size_t length = 12) {
        constexpr std::wstring_view chars = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        std::uniform_int_distribution<size_t> dist(0, chars.size() - 1);

        std::wstring rUser;
        rUser.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            rUser.push_back(chars[dist(gen)]);
        }
        return rUser;
    }

    inline std::wstring genUsers() {
        constexpr std::wstring_view users[] = {
            L"Operator", L"Admin", L"Administrator", L"OP"
        };
        std::uniform_int_distribution<size_t> dist(0, std::size(users) - 1);
        return std::wstring(users[dist(gen)]);
    }

    inline std::string genMac() {
        std::uniform_int_distribution<int> dist(0, 255);

        std::array<unsigned char, 6> mac = {};
        for (auto& byte : mac) {
            byte = static_cast<unsigned char>(dist(gen));
        }
        mac[0] &= 0xFE;

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        return std::string(macStr);
    }

    inline std::string genGUID() {
        std::uniform_int_distribution<uint32_t> dist32(0, 0xFFFFFFFF);
        std::uniform_int_distribution<uint16_t> dist16(0, 0xFFFF);

        uint32_t data1 = dist32(gen);
        uint16_t data2 = dist16(gen);
        uint16_t data3 = (dist16(gen) & 0x0FFF) | 0x4000; // ver 4 GUID
        uint16_t data4 = (dist16(gen) & 0x3FFF) | 0x8000; // var 1 GUID

        char guidStr[37];
        snprintf(guidStr, sizeof(guidStr), "%08X-%04X-%04X-%04X-%012llX",
            data1, data2, data3, data4,
            ((static_cast<uint64_t>(dist32(gen)) << 16) | dist16(gen)));

        return std::string(guidStr);
    }

    inline std::string genSerial() {
        std::uniform_int_distribution<int> dist(0, 9);

        std::string serialNumber;
        serialNumber.reserve(12);
        for (int i = 0; i < 12; ++i) {
            serialNumber.push_back('0' + dist(gen));
        }
        return serialNumber;
    }

    inline std::wstring genBaseBoardManufacturer() {
        constexpr std::wstring_view manufacturers[] = {
            L"ASUSTeK COMPUTER INC.", L"MSI", L"Gigabyte Technology Co., Ltd.", L"Dell Inc.", L"Hewlett-Packard"
        };
        std::uniform_int_distribution<size_t> dist(0, std::size(manufacturers) - 1);
        return std::wstring(manufacturers[dist(gen)]);
    }

    inline std::wstring genSystemManufacturer() {
        constexpr std::wstring_view manufacturers[] = {
            L"Dell Inc.", L"Lenovo", L"Hewlett-Packard", L"ASUSTeK COMPUTER INC.", L"Acer Inc.", L"MSI", L"Samsung Electronics"
        };
        std::uniform_int_distribution<size_t> dist(0, std::size(manufacturers) - 1);
        return std::wstring(manufacturers[dist(gen)]);
    }

    inline std::wstring genBIOSVersion() {
        std::uniform_int_distribution<int> majorDist(1, 9);
        std::uniform_int_distribution<int> minorDist(0, 9);
        std::uniform_int_distribution<int> patchDist(0, 9);

        std::wstringstream ss;
        ss << majorDist(gen) << L"." << minorDist(gen) << L"." << patchDist(gen);
        return ss.str();
    }

    inline std::wstring genBIOSReleaseDate() {
        auto now = std::chrono::system_clock::now();
        auto daysBack = std::uniform_int_distribution<int>(0, 365 * 5)(gen);
        auto randomDate = now - std::chrono::days(daysBack);
        auto timeT = std::chrono::system_clock::to_time_t(randomDate);
        std::tm timeStruct;
        localtime_s(&timeStruct, &timeT);

        std::wstringstream ss;
        ss << std::put_time(&timeStruct, L"%Y-%m-%d");
        return ss.str();
    }

    inline std::wstring genEDID() {
        std::uniform_int_distribution<int> dist(0, 0xFFFF);
        std::uniform_int_distribution<int> uidDist(10000, 99999);

        std::wostringstream idStream;
        idStream << L"5&"
            << std::hex << dist(gen)
            << dist(gen)
            << L"&0&UID"
            << uidDist(gen);

        return idStream.str();
    }

    inline std::wstring rndWindName(size_t length = 24) {
        constexpr std::wstring_view chars = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        std::uniform_int_distribution<size_t> dist(0, chars.size() - 1);

        std::wstring rndName;
        rndName.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            rndName.push_back(chars[dist(gen)]);
        }
        return rndName;
    }


    // NTDLL

    static HMODULE SeNtdll() {
        static HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        return ntdll;
    }

    static auto SeTerminateProcess() {
        static auto NtTerminateProcess = reinterpret_cast<NTSTATUS(WINAPI*)(HANDLE, NTSTATUS)>(
            GetProcAddress(SeNtdll(), "NtTerminateProcess")
            );
        return NtTerminateProcess;
    }

    static auto SeOpenKey() {
        static auto NtOpenKey = reinterpret_cast<NTSTATUS(WINAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES)>(
            GetProcAddress(SeNtdll(), "NtOpenKey")
            );
        return NtOpenKey;
    }

    static auto SeDeleteKey() {
        static auto NtDeleteKey = reinterpret_cast<NTSTATUS(WINAPI*)(HANDLE)>(
            GetProcAddress(SeNtdll(), "NtDeleteKey")
            );
        return NtDeleteKey;
    }

    static auto SeClose() {
        static auto NtClose = reinterpret_cast<NTSTATUS(WINAPI*)(HANDLE)>(
            GetProcAddress(SeNtdll(), "NtClose")
            );
        return NtClose;
    }

    inline HANDLE OpenKey(const std::wstring_view& keyPath, ACCESS_MASK desiredAccess) {
        auto NtOpenKey = SeOpenKey();
        if (!NtOpenKey) return nullptr;

        UNICODE_STRING unicodeKeyPath = {};
        OBJECT_ATTRIBUTES objectAttributes = {};

        RtlInitUnicodeString(&unicodeKeyPath, keyPath.data());
        InitializeObjectAttributes(&objectAttributes, &unicodeKeyPath, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

        HANDLE keyHandle = nullptr;
        NTSTATUS status = NtOpenKey(&keyHandle, desiredAccess, &objectAttributes);

        return NT_SUCCESS(status) ? keyHandle : nullptr;
    }

    inline void CloseKey(HANDLE keyHandle) {
        auto NtClose = SeClose();
        if (!NtClose || !keyHandle) return;

        NtClose(keyHandle);
    }

    inline bool DelKey(const std::wstring_view& keyPath) {
        HANDLE keyHandle = OpenKey(keyPath, DELETE);
        if (!keyHandle) return false;

        auto NtDeleteKey = SeDeleteKey();
        if (!NtDeleteKey) {
            CloseKey(keyHandle);
            return false;
        }

        NTSTATUS status = NtDeleteKey(keyHandle);
        CloseKey(keyHandle);

        return NT_SUCCESS(status);
    }

    static auto GetNtQueryKey() {
        static auto NtQueryKey = reinterpret_cast<NTSTATUS(WINAPI*)(
            HANDLE, KEY_INFORMATION_CLASS, PVOID, ULONG, PULONG)>(
                GetProcAddress(SeNtdll(), "NtQueryKey")
                );
        return NtQueryKey;
    }

    static auto GetNtSetValueKey() {
        static auto NtSetValueKey = reinterpret_cast<NTSTATUS(WINAPI*)(
            HANDLE, PUNICODE_STRING, ULONG, ULONG, const void*, ULONG)>(
                GetProcAddress(SeNtdll(), "NtSetValueKey")
                );
        return NtSetValueKey;
    }

    static auto GetNtEnumerateKey() {
        static auto NtEnumerateKey = reinterpret_cast<NTSTATUS(WINAPI*)(
            HANDLE, ULONG, KEY_INFORMATION_CLASS, PVOID, ULONG, PULONG)>(
                GetProcAddress(SeNtdll(), "NtEnumerateKey")
                );
        return NtEnumerateKey;
    }


    // HELPERS

    inline std::wstring GetSysDrive() {
        wchar_t systemDrive[MAX_PATH];
        if (!GetEnvironmentVariableW(L"SystemDrive", systemDrive, MAX_PATH)) {
            throw std::runtime_error("Failed to resolve system drive.");
        }
        std::wstring drivePath(systemDrive);
        if (drivePath.back() != L'\\') {
            drivePath += L'\\';
        }
        return drivePath;
    }

    inline std::wstring GetUser() {
        wchar_t userProfile[MAX_PATH];
        if (!GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH)) {
            throw std::runtime_error("Failed to resolve user profile.");
        }
        return userProfile;
    }

    inline void BulkDelete(const std::filesystem::path& dirPath, const std::vector<std::wstring>& filePatterns) {
        if (!std::filesystem::exists(dirPath)) return;
        for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
            if (entry.is_regular_file()) {
                for (const auto& pattern : filePatterns) {
                    if (entry.path().filename().wstring() == pattern) {
                        std::filesystem::remove(entry.path());
                        std::wcout << L"Deleted -> " << entry.path().wstring() << std::endl;
                    }
                }
            }
        }
    }

    inline std::wstring ResolveTarget(const std::wstring& shortcutPath) {
        IShellLinkW* shellLink = nullptr;
        IPersistFile* persistFile = nullptr;
        wchar_t target[MAX_PATH] = {};
        HRESULT coInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool coInitialized = SUCCEEDED(coInitResult);

        try {
            if (!std::filesystem::exists(shortcutPath)) {
                throw std::runtime_error("Shortcut file does not exist: " +
                    std::string(shortcutPath.begin(), shortcutPath.end()));
            }

            HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                (LPVOID*)&shellLink);
            if (FAILED(hr)) {
                throw std::runtime_error("Failed to create shell link instance.");
            }

            hr = shellLink->QueryInterface(IID_IPersistFile, (LPVOID*)&persistFile);
            if (FAILED(hr)) {
                shellLink->Release();
                throw std::runtime_error("Failed to query persist file interface.");
            }

            hr = persistFile->Load(shortcutPath.c_str(), STGM_READ);
            if (FAILED(hr)) {
                shellLink->Release();
                persistFile->Release();
                throw std::runtime_error("Failed to load shortcut file.");
            }

            hr = shellLink->GetPath(target, MAX_PATH, nullptr, 0);
            if (FAILED(hr)) {
                shellLink->Release();
                persistFile->Release();
                throw std::runtime_error("Failed to resolve shortcut target.");
            }

            shellLink->Release();
            persistFile->Release();
            if (coInitialized) CoUninitialize();

            return target;
        }
        catch (...) {
            if (shellLink) shellLink->Release();
            if (persistFile) persistFile->Release();
            if (coInitialized) CoUninitialize();
            throw;
        }
    }

    inline bool EnableDebugPrivilege() {
        HANDLE hToken;
        TOKEN_PRIVILEGES tokenPrivileges = { 0 };

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
            return false;
        }

        if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &tokenPrivileges.Privileges[0].Luid)) {
            CloseHandle(hToken);
            return false;
        }

        tokenPrivileges.PrivilegeCount = 1;
        tokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (!AdjustTokenPrivileges(hToken, FALSE, &tokenPrivileges, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr)) {
            CloseHandle(hToken);
            return false;
        }

        CloseHandle(hToken);

        return GetLastError() == ERROR_SUCCESS;
    }

    inline void SetWindow() {
        std::wstring rndName = rndWindName();
        if (!SetConsoleTitleW(rndName.c_str())) {}
    }

    inline void __TerminateRoblox() {
        SetWindow();
        EnableDebugPrivilege();

        const std::wstring_view names[] = { L"RobloxPlayerBeta.exe", L"RobloxCrashHandler.exe", L"Bloxstrap.exe", L"RobloxStudioBetaLauncher.exe", L"RobloxStudioBeta.exe" };
        const std::wstring_view titles = L"Roblox";

        auto NtTerminateProcess = SeTerminateProcess();
        if (!NtTerminateProcess) return;

        HWND hwnd = FindWindowW(nullptr, titles.data());
        if (hwnd) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
                if (hProcess) {
                    NtTerminateProcess(hProcess, 0);
                    WaitForSingleObjectEx(hProcess, INFINITE, TRUE);
                    CloseHandle(hProcess);
                }
            }
        }

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return;

        PROCESSENTRY32W entry = { sizeof(PROCESSENTRY32W) };
        std::vector<HANDLE> processHandles;

        if (Process32FirstW(snapshot, &entry)) {
            do {
                for (const auto& target : names) {
                    if (target == entry.szExeFile) {
                        HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID);
                        if (hProcess) {
                            if (std::wstring(entry.szExeFile) == L"RobloxPlayerBeta.exe") {
                                HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
                                auto NtUnmapViewOfSection = (pNtUnmapViewOfSection)GetProcAddress(hNtdll, "NtUnmapViewOfSection");

                                if (NtUnmapViewOfSection) {
                                    MEMORY_BASIC_INFORMATION mbi = { 0 };
                                    PVOID baseAddress = nullptr;

                                    while (VirtualQueryEx(hProcess, baseAddress, &mbi, sizeof(mbi)) == sizeof(mbi)) {
                                        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_IMAGE) {
                                            NtUnmapViewOfSection(hProcess, mbi.BaseAddress);
                                        }
                                        baseAddress = reinterpret_cast<PVOID>(reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize);
                                    }
                                }
                            }
                            NtTerminateProcess(hProcess, 0);
                            processHandles.push_back(hProcess);
                        }
                    }
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);

        for (HANDLE hProcess : processHandles) {
            WaitForSingleObjectEx(hProcess, INFINITE, TRUE);
            CloseHandle(hProcess);
        }

        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    for (const auto& target : names) {
                        if (target == entry.szExeFile) {
                            // ???
                        }
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
        }
    }

    // VISUALS
  
    inline void EnableANSIColors() {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode)) {
            SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }

    inline void SectHeader(const std::string& sectionName, int colorCode) {
        EnableANSIColors();
        std::cout << "\033[38;5;" << colorCode << "m"
            << "\n============ " << sectionName << " ============\n"
            << "\033[0m";
    }

    inline void TITAN() {
        std::string art = R"(
       ++x                                             +++x              
        +;++x              TITAN Spoofer            +++;+x               
         +;;;;++          8damon         +++;;;++                
         +;::::;;++                           ++;;;;;;;+                 
          +:::::::;; ++                   ++x;;:::::::+                  
          +;:::::::;  ++++             x++++ ;;::::::;+                  
           +;::::::;;  ;;;;++       ++;;;;; x;:::::::+                   
           +;::::::;;  +;::::;;   ;;::::;+  ;;::::::;+                   
            +:::::::;  +;:::::;   ;:::::;+  ;;::::::++                   
            +;::::::;;  ;:::::;   ;:::::;+  ;::::::;+                    
             +::::::;;  ;;::::;x  ;:::::;  ;;::::::++                    
             +;:::::;+  +;::::;+  ;::::;;  ;;:::::;+                     
              ;::::::;  +;::::;+  ;::::;+  +::::::;+                     
              +;:::::;  ;;::::;;  ;::::;+  ;:::::++                      
                +;:::;  +;::::;; x;::::;;  ;::::+                        
                 +;::;;  ;::::;; +;::::;;  ;::;+                         
                  +;:;;  ;;:::;; +;::::;  ;;:;+                          
                    +;;  ;;:::;; ;;:::;;  ;;+                            
                     x;+ ;;:::;; ;;:::;;  ;+                             
                         +;:::;; ;;:::;+                                 
                          ;:::;; ;;:::;+                                 
                          ;;::;; ;;:::;                                  
                          ;;::;; ;;:::;                                  
                          ;:::;; ;;:::;+                                  
                          +;:::; ;::::+                                  
                          ++:::;x;:::;+                                  
                           +;:::;;::;+                                   
                            +;::;;;;+                                    
                             ;;;;;;;+                                    
                             +;;;;;+                                     
                              +;;;+                                      
                               +;;x                                      
                               +;+                                       
                                +                                        
        )";
        std::cout << art << "\n";
    }

}