#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <wrl/client.h>
#include <Wbemidl.h>
#include <unordered_set>
#include <mutex>

#pragma comment(lib, "wbemuuid.lib")

namespace TITAN {

    class Watchdog {
    public:
        using Callback = std::function<void()>;

        explicit Watchdog(std::wstring exeName);
        ~Watchdog();

        Watchdog(const Watchdog&) = delete;
        Watchdog& operator=(const Watchdog&) = delete;

        // Start/stop the background WMI thread
        bool start();
        void stop();

        // Temporarily suspend event processing
        void pause();
        void resume();

        // Fired once when count transitions 1->0 for an armed cycle
        void setOnAllExited(Callback cb);

        int  currentCount() const noexcept { return procCount_.load(std::memory_order_relaxed); }
        bool isRunning()    const noexcept { return running_.load(std::memory_order_acquire); }

        // NEW: mark PIDs to ignore
        void addIgnoredPid(DWORD pid);

    private:
        void run_();

        bool initWmi_();
        void uninitWmi_();
        bool primeInitialCount_(); // sets procCount_
        bool setupSubscriptions_();
        bool pumpOne_(Microsoft::WRL::ComPtr<IEnumWbemClassObject>& en, bool isCreation);

        // Fix for loop-on-resume: flush queued events and re-baseline state
        void drainPending_();
        void rebaseline_();

        void onCreation_(); // ++count, arm cycle
        void onDeletion_(); // --count, fire callback if drops to 0 and armed

        bool isIgnored(DWORD pid);

    private:
        const std::wstring exeName_;

        std::thread       worker_;
        std::atomic<bool> stop_{ false };
        std::atomic<bool> running_{ false };
        std::atomic<bool> paused_{ false };
        std::atomic<bool> resync_{ false };   // when true -> drain + rebaseline

        std::atomic<int>  procCount_{ 0 };
        std::atomic<bool> armed_{ false };    // set when entering a cycle (0->1)
        Callback          onAllExited_{};

        Microsoft::WRL::ComPtr<IWbemLocator>         locator_;
        Microsoft::WRL::ComPtr<IWbemServices>        services_;
        Microsoft::WRL::ComPtr<IEnumWbemClassObject> startEnum_;
        Microsoft::WRL::ComPtr<IEnumWbemClassObject> stopEnum_;

        // NEW: ignore list
        std::unordered_set<DWORD> ignorePids_;
        std::mutex ignoreMutex_;
    };

} // namespace TITAN