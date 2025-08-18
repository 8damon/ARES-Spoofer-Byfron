#include "../Header/Mac.h"
#include "../Services/Services.hpp"

namespace MAC {

    std::wstring MacSpoofer::trim(const std::wstring& s) {
        const wchar_t* ws = L" \t\r\n";
        auto start = s.find_first_not_of(ws);
        if (start == std::wstring::npos) return {};
        auto end = s.find_last_not_of(ws);
        return s.substr(start, end - start + 1);
    }

    std::wstring MacSpoofer::GetCurrentSSID() {
        FILE* pipe = _wpopen(L"netsh wlan show interfaces", L"r");
        if (!pipe) return {};

        wchar_t buffer[512];
        std::wstring output;
        while (fgetws(buffer, _countof(buffer), pipe)) {
            output += buffer;
        }
        _pclose(pipe);

        const std::wstring marker = L"SSID                   : ";
        auto pos = output.find(marker);
        if (pos == std::wstring::npos) return {};

        auto start = pos + marker.size();
        auto end = output.find(L'\n', start);
        auto ssid = (end == std::wstring::npos)
            ? output.substr(start)
            : output.substr(start, end - start);

        return trim(ssid);
    }

    void MacSpoofer::bounceAdapter(const std::wstring& adapterName) {
        std::wstring cmdBase = L"netsh interface set interface name=\"" + adapterName + L"\" admin=";
        _wsystem((cmdBase + L"disable >nul 2>&1").c_str());
        Sleep(1000);
        _wsystem((cmdBase + L"enable  >nul 2>&1").c_str());
        Sleep(2000);

        // Reconnect Wi‑Fi to same SSID
        if (adapterName.find(L"Wi-Fi") != std::wstring::npos ||
            adapterName.find(L"Wireless") != std::wstring::npos)
        {
            if (auto ssid = GetCurrentSSID(); !ssid.empty()) {
                std::wstring reconnect = L"netsh wlan disconnect >nul 2>&1 && "
                    L"netsh wlan connect name=\"" + ssid + L"\" >nul 2>&1";
                _wsystem(reconnect.c_str());
            }
        }
    }

    std::vector<std::wstring> MacSpoofer::getAdapters() {
        ULONG bufLen = 0;
        GetAdaptersInfo(nullptr, &bufLen);
        if (bufLen == 0) return {};

        std::vector<BYTE> buf(bufLen);
        auto pInfo = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
        if (GetAdaptersInfo(pInfo, &bufLen) != NO_ERROR) return {};

        std::vector<std::wstring> adapters;
        for (auto p = pInfo; p; p = p->Next) {
            adapters.emplace_back(
                p->Description,
                p->Description + strlen(p->Description)
            );
        }
        return adapters;
    }

    std::optional<std::wstring> MacSpoofer::resAdapter(const std::wstring& adapterName) {
        COM::COMInitializer comInit;
        IWbemLocator* pLoc = nullptr;
        IWbemServices* pSvc = nullptr;

        if (FAILED(CoCreateInstance(
            CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<LPVOID*>(&pLoc))))
            return {};

        _bstr_t ns(L"ROOT\\CIMV2");
        if (FAILED(pLoc->ConnectServer(
            ns, nullptr, nullptr, 0, 0, nullptr, nullptr, &pSvc)))
        {
            pLoc->Release();
            return {};
        }

        std::wstring q = L"SELECT * FROM Win32_NetworkAdapter WHERE Name = '" +
            adapterName + L"'";
        IEnumWbemClassObject* pEnum = nullptr;
        if (FAILED(pSvc->ExecQuery(
            _bstr_t(L"WQL"), _bstr_t(q.c_str()),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &pEnum)))
        {
            pSvc->Release();
            pLoc->Release();
            return {};
        }

        IWbemClassObject* pObj = nullptr;
        ULONG ret = 0;
        std::optional<std::wstring> guid;
        if (SUCCEEDED(pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret)) && ret) {
            VARIANT vt;
            VariantInit(&vt);
            if (SUCCEEDED(pObj->Get(L"GUID", 0, &vt, nullptr, nullptr)) &&
                vt.vt == VT_BSTR)
            {
                guid = std::wstring(vt.bstrVal);
            }
            VariantClear(&vt);
            pObj->Release();
        }

        pEnum->Release();
        pSvc->Release();
        pLoc->Release();
        return guid;
    }

    std::wstring MacSpoofer::getAdapterRegPath(const std::wstring& adapterGUID) {
        static const std::wstring base =
            L"SYSTEM\\CurrentControlSet\\Control\\Class\\"
            L"{4D36E972-E325-11CE-BFC1-08002BE10318}";
        HKEY hKey;
        if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, base.c_str(),
            0, KEY_READ, &hKey) != ERROR_SUCCESS)
            return {};

        WCHAR name[256];
        DWORD idx = 0, nameLen = _countof(name);
        while (RegEnumKeyEx(hKey, idx++, name, &nameLen,
            nullptr, nullptr, nullptr, nullptr) != ERROR_NO_MORE_ITEMS)
        {
            std::wstring path = base + L"\\" + name;
            HKEY sub;
            if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, path.c_str(),
                0, KEY_READ, &sub) == ERROR_SUCCESS)
            {
                WCHAR guidVal[256];
                DWORD guidLen = sizeof(guidVal);
                if (RegQueryValueEx(sub, L"NetCfgInstanceId",
                    nullptr, nullptr,
                    reinterpret_cast<BYTE*>(guidVal),
                    &guidLen) == ERROR_SUCCESS &&
                    adapterGUID == guidVal)
                {
                    RegCloseKey(sub);
                    RegCloseKey(hKey);
                    return path;
                }
                RegCloseKey(sub);
            }
            nameLen = _countof(name);
        }
        RegCloseKey(hKey);
        return {};
    }

    void MacSpoofer::spoofMac() {
        auto adapters = getAdapters();
        if (adapters.empty()) return;

        std::mutex outputMutex;
        std::vector<std::thread> threads;

        for (auto& adapter : adapters) {
            threads.emplace_back([&, adapter]() {
                COM::COMInitializer comInit;
                auto guid = resAdapter(adapter);
                if (!guid) return;

                auto regPath = getAdapterRegPath(*guid);
                if (regPath.empty()) return;

                auto newMac = TsService::genMac();
                std::wstring macStr(newMac.begin(), newMac.end());

                // Write to registry
                HKEY hKey;
                if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                    regPath.c_str(),
                    0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
                {
                    RegSetValueEx(hKey, L"NetworkAddress",
                        0, REG_SZ,
                        reinterpret_cast<const BYTE*>(macStr.c_str()),
                        static_cast<DWORD>((macStr.size() + 1) * sizeof(wchar_t)));
                    RegCloseKey(hKey);

                    // Optional verification
                    HKEY hVerify;
                    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                        regPath.c_str(),
                        0, KEY_READ, &hVerify) == ERROR_SUCCESS)
                    {
                        WCHAR check[256]; DWORD len = sizeof(check);
                        RegQueryValueEx(hVerify, L"NetworkAddress",
                            nullptr, nullptr,
                            reinterpret_cast<BYTE*>(check), &len);
                        RegCloseKey(hVerify);

                        if (macStr != check) {
                            std::lock_guard<std::mutex> lk(outputMutex);
                            std::wcerr << L"[!] Verification failed for " << adapter << L"\n";
                            return;
                        }
                    }
                }

                {
                    std::lock_guard<std::mutex> lk(outputMutex);
                    std::wcout << L"Spoofed -> " << adapter
                        << L", New MAC -> " << macStr << L"\n";
                }

                // Bounce adapter (in its own thread)
                std::thread(&MacSpoofer::bounceAdapter, adapter).detach();
                });
        }

        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    }

    void MacSpoofer::run() {
        TsService::SectHeader("MAC Spoofing", 196);
        spoofMac();
    }
}