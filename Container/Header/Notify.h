#pragma once
#include <string>
#include <vector>

namespace TITAN {

    class Notification {
    public:
        Notification(std::wstring appId = L"TITAN.Spoofer",
            std::wstring appName = L"TITAN Spoofer");
        ~Notification();

        // Buttons: { {button_text, argument_suffix} } -> titan-notify:<suffix>
        bool NotifyDesktop(const std::wstring& title,
            const std::wstring& message,
            const std::vector<std::pair<std::wstring, std::wstring>>& actions = {});
        bool Initialize(); // COM/WinRT + AUMID + StartMenu shortcut + URL protocol

        // Call at program start. If this instance was launched via titan-notify: URL,
        // it signals the primary instance and returns true (caller should exit).
        static bool HandleProtocolIfPresentAndExitEarly();

    private:
        bool ensureShortcut_();
        bool ensureWinRT_();
        bool ensureCOM_();
        void uninitWinRT_();
        void uninitCOM_();
        bool ensureUrlProtocol_(); // registers titan-notify: URL protocol
        static std::wstring escapeXml_(const std::wstring& in);

    private:
        std::wstring appId_;
        std::wstring appName_;
        bool comInit_{ false };
        bool winrtInit_{ false };
        bool shortcutOk_{ false };
    };

} // namespace TITAN