#include "../Header/Mac.h"

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00 // Windows 10+

#include <windows.h>
#include <wlanapi.h>
#include <windot11.h>
#include <objbase.h>
#include <comdef.h>
#include <msxml6.h>
#include <atlbase.h>
#include <iphlpapi.h>

#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <mutex>
#include <thread>
#include <random>
#include <algorithm>

#pragma comment(lib, "Wlanapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Uuid.lib")
#pragma comment(lib, "Msxml6.lib")
#pragma comment(lib, "Iphlpapi.lib")

namespace MAC {

    struct ComInit {
        HRESULT hr;
        ComInit() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
        ~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
        bool ok() const { return SUCCEEDED(hr); }
    };

    static inline std::wstring trim(const std::wstring& s) {
        static const wchar_t* ws = L" \t\r\n";
        const auto b = s.find_first_not_of(ws);
        if (b == std::wstring::npos) return {};
        const auto e = s.find_last_not_of(ws);
        return s.substr(b, e - b + 1);
    }

    static inline std::wstring toUpper(std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), ::towupper);
        return s;
    }

    static std::wstring GenMac12() {
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_int_distribution<int> dist(0, 255);

        unsigned char mac[6];
        for (int i = 0; i < 6; ++i) mac[i] = static_cast<unsigned char>(dist(rng));

        mac[0] &= 0xFE; // clear LSB (multicast)
        mac[0] |= 0x02; // set LAA bit

        wchar_t buf[13] = { 0 };
        _snwprintf_s(buf, _TRUNCATE, L"%02X%02X%02X%02X%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return buf;
    }

    static void BounceAdapterByName(const std::wstring& friendlyName) {
        const std::wstring cmdBase = L"netsh interface set interface name=\"" + friendlyName + L"\" admin=";
        _wsystem((cmdBase + L"disable >nul 2>&1").c_str());
        Sleep(1000);
        _wsystem((cmdBase + L"enable  >nul 2>&1").c_str());
        Sleep(2000);
    }

    static bool EnsureRandomMacInProfileXml(std::wstring& xml) {
        CComPtr<IXMLDOMDocument3> doc;
        if (FAILED(doc.CoCreateInstance(CLSID_DOMDocument60))) return false;

        VARIANT_BOOL ok = VARIANT_FALSE;
        doc->put_async(VARIANT_FALSE);
        if (FAILED(doc->loadXML(CComBSTR(xml.c_str()), &ok)) || ok != VARIANT_TRUE)
            return false;

        doc->setProperty(CComBSTR(L"SelectionLanguage"), CComVariant(L"XPath"));
        doc->setProperty(CComBSTR(L"SelectionNamespaces"),
            CComVariant(L"xmlns:wlan='http://www.microsoft.com/networking/WLAN/profile/v1' "
                L"xmlns:wlan3='http://www.microsoft.com/networking/WLAN/profile/v3'"));

        CComPtr<IXMLDOMNode> root;
        if (FAILED(doc->selectSingleNode(CComBSTR(L"/wlan:WLANProfile"), &root)) || !root)
            return false;

        CComPtr<IXMLDOMNode> macNode;
        doc->selectSingleNode(CComBSTR(L"/wlan:WLANProfile/wlan3:MacRandomization"), &macNode);
        if (!macNode) {
            CComPtr<IXMLDOMElement> macEl;
            if (FAILED(doc->createNode(
                CComVariant(NODE_ELEMENT),
                CComBSTR(L"MacRandomization"),
                CComBSTR(L"http://www.microsoft.com/networking/WLAN/profile/v3"),
                (IXMLDOMNode**)&macEl))) return false;
            root->appendChild(macEl, &macNode);
        }

        auto setChildText = [&](const wchar_t* name, const wchar_t* text) -> bool {
            std::wstring xp = L"/wlan:WLANProfile/wlan3:MacRandomization/wlan3:";
            xp += name;
            CComPtr<IXMLDOMNode> node;
            doc->selectSingleNode(CComBSTR(xp.c_str()), &node);
            if (!node) {
                CComPtr<IXMLDOMElement> el;
                if (FAILED(doc->createNode(
                    CComVariant(NODE_ELEMENT),
                    CComBSTR(name),
                    CComBSTR(L"http://www.microsoft.com/networking/WLAN/profile/v3"),
                    (IXMLDOMNode**)&el))) return false;
                if (text) el->put_text(CComBSTR(text));
                macNode->appendChild(el, nullptr);
            }
            else {
                node->put_text(CComBSTR(text));
            }
            return true;
            };

        if (!setChildText(L"enableRandomization", L"true"))  return false;
        if (!setChildText(L"randomizeEveryday", L"false")) return false;

        std::mt19937 rng{ std::random_device{}() };
        const unsigned seed = rng();
        wchar_t buf[32]; _snwprintf_s(buf, _TRUNCATE, L"%u", seed);
        if (!setChildText(L"randomizationSeed", buf)) return false;

        CComBSTR outXml;
        if (FAILED(doc->get_xml(&outXml))) return false;
        xml.assign(outXml, outXml.Length());
        return true;
    }

    static bool ReconnectPinned(HANDLE h, const GUID& ifGuid,
        const wchar_t* profileName,
        const DOT11_MAC_ADDRESS& bssid)
    {

        struct BSSID_LIST_ONE {
            NDIS_OBJECT_HEADER Header;
            ULONG uNumOfEntries;
            ULONG uTotalNumOfEntries;
            DOT11_MAC_ADDRESS BSSIDs[1];
        } list{};

        list.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        list.Header.Revision = DOT11_BSSID_LIST_REVISION_1;
        list.Header.Size = sizeof(BSSID_LIST_ONE);
        list.uNumOfEntries = 1;
        list.uTotalNumOfEntries = 1;
        memcpy(list.BSSIDs[0], bssid, sizeof(DOT11_MAC_ADDRESS));

        WLAN_CONNECTION_PARAMETERS p{};
        p.wlanConnectionMode = wlan_connection_mode_profile;
        p.strProfile = profileName;
        p.pDesiredBssidList = (PDOT11_BSSID_LIST)&list;
        p.dot11BssType = dot11_BSS_type_infrastructure;

        return WlanConnect(h, &ifGuid, &p, nullptr) == ERROR_SUCCESS;
    }

    static void SpoofWifiForConnectedInterfaces() {
        HANDLE h = nullptr;
        DWORD ver = 0;
        if (WlanOpenHandle(2, nullptr, &ver, &h) != ERROR_SUCCESS) {
            std::wcerr << L"[!] WlanOpenHandle failed\n";
            return;
        }

        PWLAN_INTERFACE_INFO_LIST pList = nullptr;
        if (WlanEnumInterfaces(h, nullptr, &pList) != ERROR_SUCCESS || !pList) {
            std::wcerr << L"[!] WlanEnumInterfaces failed\n";
            WlanCloseHandle(h, nullptr);
            return;
        }

        for (DWORD i = 0; i < pList->dwNumberOfItems; ++i) {
            const auto& ifinfo = pList->InterfaceInfo[i];
            if (ifinfo.isState != wlan_interface_state_connected)
                continue;

            PWLAN_CONNECTION_ATTRIBUTES pConn = nullptr;
            DWORD dataSize = 0;
            WLAN_OPCODE_VALUE_TYPE opt = wlan_opcode_value_type_invalid;
            if (WlanQueryInterface(
                h, &ifinfo.InterfaceGuid, wlan_intf_opcode_current_connection,
                nullptr, &dataSize, (PVOID*)&pConn, &opt) != ERROR_SUCCESS || !pConn) {
                std::wcerr << L"[!] WlanQueryInterface failed (current_connection)\n";
                continue;
            }

            DOT11_MAC_ADDRESS bssid{};
            memcpy(bssid, pConn->wlanAssociationAttributes.dot11Bssid, sizeof(DOT11_MAC_ADDRESS));
            std::wstring profile = pConn->strProfileName ? pConn->strProfileName : L"";
            WlanFreeMemory(pConn);

            if (profile.empty()) {
                std::wcerr << L"[!] Empty profile name, skipping interface\n";
                continue;
            }

            DWORD flags = 0, access = 0; LPWSTR pXml = nullptr;
            if (WlanGetProfile(h, &ifinfo.InterfaceGuid, profile.c_str(),
                nullptr, &pXml, &flags, &access) != ERROR_SUCCESS || !pXml) {
                std::wcerr << L"[!] WlanGetProfile failed for profile " << profile << L"\n";
                continue;
            }
            std::wstring xml = pXml;
            WlanFreeMemory(pXml);

            if (!EnsureRandomMacInProfileXml(xml)) {
                std::wcerr << L"[!] Failed to modify WLAN profile XML for " << profile << L"\n";
                continue;
            }

            DWORD reason = 0;
            if (WlanSetProfile(h, &ifinfo.InterfaceGuid, 0 /* user */,
                xml.c_str(), nullptr, TRUE /* overwrite */,
                nullptr, &reason) != ERROR_SUCCESS) {
                std::wcerr << L"[!] WlanSetProfile failed (reason=" << reason << L") for " << profile << L"\n";
                continue;
            }

            std::wcout << L"[i] Reconnecting " << profile << L" with randomized MAC...\n";
            if (!ReconnectPinned(h, ifinfo.InterfaceGuid, profile.c_str(), bssid)) {
                std::wcerr << L"[!] Pinned reconnect failed; trying normal reconnect...\n";
                WLAN_CONNECTION_PARAMETERS p{};
                p.wlanConnectionMode = wlan_connection_mode_profile;
                p.strProfile = profile.c_str();
                p.dot11BssType = dot11_BSS_type_infrastructure;
                WlanConnect(h, &ifinfo.InterfaceGuid, &p, nullptr);
            }
        }

        WlanFreeMemory(pList);
        WlanCloseHandle(h, nullptr);
    }

    // ========= Ethernet / Virtual (v2) path (modernized) =========

    // Utility: enumerate adapters with GetAdaptersAddresses (Unicode-friendly)
    struct AdapterInfo {
        std::wstring friendlyName; // "Ethernet", "Wi-Fi", "vEthernet (Default Switch)", etc.
        std::wstring guid;         // "{GUID}" from AdapterName or converted
        ULONG ifType;              // IF_TYPE_*
    };

    static std::vector<AdapterInfo> EnumAllAdapters() {
        std::vector<AdapterInfo> out;

        ULONG bufLen = 16 * 1024;
        std::vector<BYTE> buf(bufLen);
        IP_ADAPTER_ADDRESSES* p = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        ULONG flags = GAA_FLAG_INCLUDE_ALL_INTERFACES | GAA_FLAG_INCLUDE_ALL_COMPARTMENTS;

        ULONG rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, p, &bufLen);
        if (rc == ERROR_BUFFER_OVERFLOW) {
            buf.resize(bufLen);
            p = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
            rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, p, &bufLen);
        }
        if (rc != NO_ERROR) {
            std::wcerr << L"[!] GetAdaptersAddresses failed: " << rc << L"\n";
            return out;
        }

        for (auto cur = p; cur; cur = cur->Next) {

            if (cur->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

            std::wstring guidW;
            if (cur->AdapterName && cur->AdapterName[0]) {
                size_t len = strlen(cur->AdapterName);
                std::wstring tmp; tmp.reserve(len);
                for (size_t i = 0; i < len; ++i) tmp.push_back((wchar_t)(unsigned char)cur->AdapterName[i]);
                if (tmp.size() && tmp.front() != L'{') {
                    auto lb = tmp.find(L'{'); auto rb = tmp.find(L'}', lb == std::wstring::npos ? 0 : lb);
                    if (lb != std::wstring::npos && rb != std::wstring::npos)
                        guidW = tmp.substr(lb, rb - lb + 1);
                    else
                        guidW = tmp;
                }
                else {
                    guidW = tmp;
                }
            }

            out.push_back(AdapterInfo{
                cur->FriendlyName ? cur->FriendlyName : L"",
                guidW,
                cur->IfType
                });
        }
        return out;
    }

    static std::wstring GetAdapterRegPathByGUID(const std::wstring& guid) {
        static const std::wstring base =
            L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}";
        HKEY hBase;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, base.c_str(), 0,
            KEY_READ | KEY_WOW64_64KEY, &hBase) != ERROR_SUCCESS)
            return {};

        DWORD idx = 0;
        wchar_t name[256];
        DWORD nameLen = _countof(name);

        while (true) {
            nameLen = _countof(name);
            const auto enumRc = RegEnumKeyExW(hBase, idx++, name, &nameLen, nullptr, nullptr, nullptr, nullptr);
            if (enumRc == ERROR_NO_MORE_ITEMS) break;
            if (enumRc != ERROR_SUCCESS) continue;

            const std::wstring subPath = base + L"\\" + name;
            HKEY hSub;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subPath.c_str(), 0,
                KEY_READ | KEY_WOW64_64KEY, &hSub) == ERROR_SUCCESS)
            {
                wchar_t id[256]; DWORD cb = sizeof(id);
                if (RegQueryValueExW(hSub, L"NetCfgInstanceId", nullptr, nullptr,
                    reinterpret_cast<BYTE*>(id), &cb) == ERROR_SUCCESS)
                {
                    if (_wcsicmp(id, guid.c_str()) == 0) {
                        RegCloseKey(hSub);
                        RegCloseKey(hBase);
                        return subPath;
                    }
                }
                RegCloseKey(hSub);
            }
        }
        RegCloseKey(hBase);
        return {};
    }

    static bool SetAdapterNetworkAddress(const std::wstring& regPath, const std::wstring& mac12) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS)
            return false;

        const std::wstring macU = toUpper(mac12);
        const DWORD cb = static_cast<DWORD>((macU.size() + 1) * sizeof(wchar_t));
        const LONG rc = RegSetValueExW(hKey, L"NetworkAddress", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(macU.c_str()), cb);
        RegCloseKey(hKey);
        return (rc == ERROR_SUCCESS);
    }

    static void SpoofNonWifiAdapters() {
        auto adapters = EnumAllAdapters();
        if (adapters.empty()) {
            std::wcout << L"[i] No adapters found.\n";
            return;
        }

        std::mutex outmtx;
        std::vector<std::thread> workers;

        for (const auto& a : adapters) {
            if (a.ifType == IF_TYPE_IEEE80211) continue;
            if (a.guid.empty() || a.friendlyName.empty()) continue;
            if (a.ifType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

            workers.emplace_back([a, &outmtx]() {
                const auto regPath = GetAdapterRegPathByGUID(a.guid);
                if (regPath.empty()) {
                    std::lock_guard<std::mutex> lk(outmtx);
                    std::wcerr << L"[!] Could not resolve registry path for " << a.friendlyName << L" (" << a.guid << L")\n";
                    return;
                }

                const auto mac = GenMac12();
                if (!SetAdapterNetworkAddress(regPath, mac)) {
                    std::lock_guard<std::mutex> lk(outmtx);
                    std::wcerr << L"[!] Failed to set NetworkAddress for " << a.friendlyName << L"\n";
                    return;
                }

                {
                    std::lock_guard<std::mutex> lk(outmtx);
                    std::wcout << L"[+] Spoofed " << a.friendlyName << L" (" << a.guid
                        << L") -> " << mac << L"\n";
                }

                BounceAdapterByName(a.friendlyName);
                });
        }

        for (auto& t : workers) if (t.joinable()) t.join();
    }

    void MacSpoofer::run() {
        ComInit ci;
        if (!ci.ok()) {
            std::wcerr << L"[!] COM initialization failed\n";
            return;
        }

        std::wcout << L"==[ Wi-Fi (per-SSID randomization via WLAN profile) ]==\n";
        SpoofWifiForConnectedInterfaces();

        std::wcout << L"\n==[ Ethernet / Virtual (registry NetworkAddress) ]==\n";
        SpoofNonWifiAdapters();

        std::wcout << L"\n[i] Done.\n";
    }
}