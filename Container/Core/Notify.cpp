#include "../Header/Notify.h"

#include <windows.h>
#include <roapi.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <vector>

#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "runtimeobject.lib")

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
using namespace ABI::Windows::Data::Xml::Dom;
using namespace ABI::Windows::UI::Notifications;

namespace TITAN {

    static inline void SetProcessAumid(const std::wstring& appId) {
        SetCurrentProcessExplicitAppUserModelID(appId.c_str());
    }

    // ---------- constants for simple IPC via named events ----------
    static constexpr wchar_t kEventYes[] = L"Local\\TITAN_SPOOF_YES";
    static constexpr wchar_t kEventDismiss[] = L"Local\\TITAN_SPOOF_DISMISS";
    static constexpr wchar_t kScheme[] = L"titan-notify"; // URL scheme we register

    Notification::Notification(std::wstring appId, std::wstring appName)
        : appId_(std::move(appId)), appName_(std::move(appName)) {
    }

    Notification::~Notification() {
        uninitWinRT_();
        uninitCOM_();
    }

    bool Notification::Initialize() {
        if (!ensureCOM_())  return false;
        SetProcessAumid(appId_);
        shortcutOk_ = ensureShortcut_();
        if (!ensureUrlProtocol_()) return false;   // <-- ensure titan-notify: is registered
        return ensureWinRT_();
    }

    bool Notification::NotifyDesktop(const std::wstring& title,
        const std::wstring& message,
        const std::vector<std::pair<std::wstring, std::wstring>>& actions)
    {
        if (!comInit_ && !ensureCOM_()) return false;
        SetProcessAumid(appId_);
        if (!shortcutOk_) shortcutOk_ = ensureShortcut_();
        if (!winrtInit_ && !ensureWinRT_()) return false;

        const auto ttl = escapeXml_(title);
        const auto msg = escapeXml_(message);

        std::wstring xml =
            L"<toast duration='short'>"
            L"<visual>"
            L"<binding template='ToastGeneric'>"
            L"<text>" + ttl + L"</text>"
            L"<text>" + msg + L"</text>"
            L"</binding>"
            L"</visual>";

        if (!actions.empty()) {
            xml += L"<actions>";
            for (const auto& act : actions) {
                // act.second becomes titan-notify:<suffix>
                std::wstring uri = std::wstring(kScheme) + L":" + act.second;
                xml += L"<action activationType='protocol' "
                    L"content='" + escapeXml_(act.first) + L"' "
                    L"arguments='" + escapeXml_(uri) + L"'/>";
            }
            xml += L"</actions>";
        }

        xml += L"</toast>";

        // Build XmlDocument
        ComPtr<IInspectable> insp;
        HRESULT hr = RoActivateInstance(
            HStringReference(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument).Get(),
            insp.GetAddressOf());
        if (FAILED(hr)) return false;

        ComPtr<IXmlDocument> doc;
        hr = insp.As(&doc);
        if (FAILED(hr)) return false;

        ComPtr<IXmlDocumentIO> docIo;
        hr = doc.As(&docIo);
        if (FAILED(hr)) return false;

        hr = docIo->LoadXml(HStringReference(xml.c_str()).Get());
        if (FAILED(hr)) return false;

        // Toast factories
        ComPtr<IToastNotificationManagerStatics> mgr;
        hr = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(),
            IID_PPV_ARGS(&mgr));
        if (FAILED(hr)) return false;

        ComPtr<IToastNotifier> notifier;
        hr = mgr->CreateToastNotifierWithId(HStringReference(appId_.c_str()).Get(), &notifier);
        if (FAILED(hr)) return false;

        ComPtr<IToastNotificationFactory> factory;
        hr = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotification).Get(),
            IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return false;

        ComPtr<IToastNotification> toast;
        hr = factory->CreateToastNotification(doc.Get(), &toast);
        if (FAILED(hr)) return false;

        hr = notifier->Show(toast.Get());
        return SUCCEEDED(hr);
    }

    bool Notification::ensureCOM_() {
        if (comInit_) return true;
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
        comInit_ = true;
        return true;
    }

    bool Notification::ensureWinRT_() {
        if (winrtInit_) return true;
        HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
        if (FAILED(hr) && hr != S_OK && hr != S_FALSE && hr != 0x80010106 /*RPC_E_CHANGED_MODE*/) {
            return false;
        }
        winrtInit_ = true;
        return true;
    }

    void Notification::uninitCOM_() {
        if (comInit_) {
            CoUninitialize();
            comInit_ = false;
        }
    }

    void Notification::uninitWinRT_() {
        if (winrtInit_) {
            RoUninitialize();
            winrtInit_ = false;
        }
    }

    // Start Menu\Programs shortcut with AppUserModelID
    bool Notification::ensureShortcut_() {
        PWSTR programsPath = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programsPath))) {
            return false;
        }
        std::wstring lnkPath = std::wstring(programsPath) + L"\\" + appName_ + L".lnk";
        CoTaskMemFree(programsPath);

        DWORD attrs = GetFileAttributesW(lnkPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            return true; // exists; assume OK
        }

        wchar_t exePath[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return false;

        ComPtr<IShellLinkW> sl;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl));
        if (FAILED(hr)) return false;

        sl->SetPath(exePath);
        sl->SetDescription(appName_.c_str());
        sl->SetIconLocation(exePath, 0);

        ComPtr<IPropertyStore> props;
        hr = sl.As(&props);
        if (FAILED(hr)) return false;

        PROPVARIANT pv{};
        if (FAILED(InitPropVariantFromString(appId_.c_str(), &pv))) return false;
        hr = props->SetValue(PKEY_AppUserModel_ID, pv);
        PropVariantClear(&pv);
        if (FAILED(hr)) return false;

        hr = props->Commit();
        if (FAILED(hr)) return false;

        ComPtr<IPersistFile> pf;
        hr = sl.As(&pf);
        if (FAILED(hr)) return false;

        hr = pf->Save(lnkPath.c_str(), TRUE);
        return SUCCEEDED(hr);
    }

    // ---------- URL protocol registration: titan-notify: ----------
    bool Notification::ensureUrlProtocol_() {
        // HKCU\Software\Classes\titan-notify\ (Default) = "TITAN Notification Protocol"
        // HKCU\Software\Classes\titan-notify\URL Protocol = ""
        // HKCU\Software\Classes\titan-notify\shell\open\command\(Default) = "<exe>" "%1"
        wchar_t exePath[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return false;

        std::wstring base = L"Software\\Classes\\";
        base += kScheme;

        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, base.c_str(), 0, nullptr, 0,
            KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_QUERY_VALUE,
            nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
            return false;
        }

        const wchar_t* friendly = L"TITAN Notification Protocol";
        RegSetValueExW(hKey, nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(friendly),
            static_cast<DWORD>((wcslen(friendly) + 1) * sizeof(wchar_t)));

        // Presence (even empty) of "URL Protocol" marks this as a protocol handler
        const wchar_t empty[] = L"";
        RegSetValueExW(hKey, L"URL Protocol", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(empty),
            static_cast<DWORD>(sizeof(wchar_t)));

        // command
        std::wstring cmdKey = base + L"\\shell\\open\\command";
        HKEY hCmd{};
        if (RegCreateKeyExW(HKEY_CURRENT_USER, cmdKey.c_str(), 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &hCmd, nullptr) == ERROR_SUCCESS) {
            std::wstring cmd = L"\"";
            cmd += exePath;
            cmd += L"\" \"%1\"";
            RegSetValueExW(hCmd, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(cmd.c_str()),
                static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hCmd);
        }

        RegCloseKey(hKey);
        return true;
    }

    // ---------- Secondary-instance handler: parse titan-notify: URL and signal events ----------
    bool Notification::HandleProtocolIfPresentAndExitEarly() {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        bool handled = false;

        if (argv && argc >= 2) {
            std::wstring arg = argv[1];
            // Accept titan-notify:<anything>
            if (arg.rfind(kScheme, 0) == 0 && arg.size() > wcslen(kScheme) + 1 && arg[wcslen(kScheme)] == L':') {
                // crude parse: look for "spoof" or "dismiss"
                DWORD access = EVENT_MODIFY_STATE;
                if (arg.find(L"spoof") != std::wstring::npos) {
                    if (HANDLE h = OpenEventW(access, FALSE, kEventYes)) {
                        SetEvent(h);
                        CloseHandle(h);
                    }
                    handled = true;
                }
                else if (arg.find(L"dismiss") != std::wstring::npos) {
                    if (HANDLE h = OpenEventW(access, FALSE, kEventDismiss)) {
                        SetEvent(h);
                        CloseHandle(h);
                    }
                    handled = true;
                }
            }
        }

        if (argv) LocalFree(argv);
        return handled;
    }

    std::wstring Notification::escapeXml_(const std::wstring& in) {
        std::wstring out; out.reserve(in.size());
        for (wchar_t ch : in) {
            switch (ch) {
            case L'&':  out += L"&amp;";  break;
            case L'<':  out += L"&lt;";   break;
            case L'>':  out += L"&gt;";   break;
            case L'\"': out += L"&quot;"; break;
            case L'\'': out += L"&apos;"; break;
            default:    out += ch;        break;
            }
        }
        return out;
    }

} // namespace TITAN