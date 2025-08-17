#include "../Header/Watchdog.h"

#include <comdef.h>
#include <chrono>

using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

namespace TITAN {

    static inline void setProxy(IUnknown* unk) {
        if (!unk) return;
        CoSetProxyBlanket(
            unk,
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            nullptr,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE
        );
    }

    Watchdog::Watchdog(std::wstring exeName)
        : exeName_(std::move(exeName)) {
    }

    Watchdog::~Watchdog() { stop(); }

    bool Watchdog::start() {
        bool expect = false;
        if (!running_.compare_exchange_strong(expect, true, std::memory_order_acq_rel)) {
            return true; // already running
        }
        stop_.store(false, std::memory_order_release);
        worker_ = std::thread([this] { run_(); });
        return true;
    }

    void Watchdog::stop() {
        if (!running_.load(std::memory_order_acquire)) return;
        stop_.store(true, std::memory_order_release);
        if (worker_.joinable()) worker_.join();
        running_.store(false, std::memory_order_release);
    }

    void Watchdog::pause() {
        paused_.store(true, std::memory_order_release);
        resync_.store(true, std::memory_order_release); // request drain while paused
    }

    void Watchdog::resume() {
        // ensure a fresh drain+baseline right before we resume pumping
        resync_.store(true, std::memory_order_release);
        paused_.store(false, std::memory_order_release);
    }

    void Watchdog::setOnAllExited(Callback cb) {
        onAllExited_ = std::move(cb);
    }

    void Watchdog::run_() {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool needUninit = SUCCEEDED(co);

        // It's fine if someone initialized security already
        HRESULT sec = CoInitializeSecurity(
            nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr, EOAC_NONE, nullptr
        );
        if (FAILED(sec) && sec != RPC_E_TOO_LATE) {
            goto Exit;
        }

        if (!initWmi_()) goto Exit;
        (void)primeInitialCount_();
        if (!setupSubscriptions_()) goto Exit;

        // If we start with >0 instances, we're inside a cycle already
        if (procCount_.load(std::memory_order_acquire) > 0) {
            armed_.store(true, std::memory_order_release);
        }

        while (!stop_.load(std::memory_order_acquire)) {
            // Handle resync regardless of pause state
            if (resync_.exchange(false, std::memory_order_acq_rel)) {
                drainPending_();
                rebaseline_();
            }

            if (paused_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(200ms);
                continue;
            }

            bool got = false;
            if (startEnum_) got |= pumpOne_(startEnum_, true);
            if (stopEnum_)  got |= pumpOne_(stopEnum_, false);

            if (!got) std::this_thread::sleep_for(200ms); // idle backoff
        }

    Exit:
        uninitWmi_();
        if (needUninit) CoUninitialize();
    }

    bool Watchdog::initWmi_() {
        HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(locator_.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) return false;

        hr = locator_->ConnectServer(
            _bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr,
            0, nullptr, nullptr, services_.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;

        setProxy(services_.Get());
        return true;
    }

    void Watchdog::uninitWmi_() {
        startEnum_.Reset();
        stopEnum_.Reset();
        services_.Reset();
        locator_.Reset();
    }

    bool Watchdog::primeInitialCount_() {
        if (!services_) return false;

        wchar_t query[256];
        swprintf_s(query, L"SELECT ProcessId FROM Win32_Process WHERE Name='%s'", exeName_.c_str());

        ComPtr<IEnumWbemClassObject> en;
        HRESULT hr = services_->ExecQuery(
            _bstr_t(L"WQL"),
            _bstr_t(query),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr,
            en.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;
        setProxy(en.Get());

        ULONG ret = 0;
        ComPtr<IWbemClassObject> obj;
        int cnt = 0;
        while (en->Next(1000, 1, obj.ReleaseAndGetAddressOf(), &ret) == WBEM_S_NO_ERROR && ret == 1) {
            ++cnt;
        }
        procCount_.store(cnt, std::memory_order_release);
        return true;
    }

    bool Watchdog::setupSubscriptions_() {
        if (!services_) return false;

        wchar_t qStart[512], qStop[512];
        swprintf_s(qStart,
            L"SELECT * FROM __InstanceCreationEvent WITHIN 1 "
            L"WHERE TargetInstance ISA 'Win32_Process' AND TargetInstance.Name='%s'",
            exeName_.c_str());

        swprintf_s(qStop,
            L"SELECT * FROM __InstanceDeletionEvent WITHIN 1 "
            L"WHERE TargetInstance ISA 'Win32_Process' AND TargetInstance.Name='%s'",
            exeName_.c_str());

        HRESULT hr = services_->ExecNotificationQuery(
            _bstr_t(L"WQL"), _bstr_t(qStart),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr,
            startEnum_.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;
        setProxy(startEnum_.Get());

        hr = services_->ExecNotificationQuery(
            _bstr_t(L"WQL"), _bstr_t(qStop),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr,
            stopEnum_.ReleaseAndGetAddressOf()
        );
        if (FAILED(hr)) return false;
        setProxy(stopEnum_.Get());

        return true;
    }

    bool Watchdog::pumpOne_(Microsoft::WRL::ComPtr<IEnumWbemClassObject>& en, bool isCreation) {
        if (!en) return false;

        ComPtr<IWbemClassObject> ev;
        ULONG ret = 0;

        HRESULT hr = en->Next(200, 1, ev.ReleaseAndGetAddressOf(), &ret);
        if (hr != WBEM_S_NO_ERROR || ret == 0) return false;

        VARIANT vt; VariantInit(&vt);
        HRESULT hrGet = ev->Get(L"TargetInstance", 0, &vt, nullptr, nullptr);
        if (SUCCEEDED(hrGet) && vt.vt == VT_UNKNOWN && vt.punkVal) {
            ComPtr<IWbemClassObject> proc;
            vt.punkVal->QueryInterface(IID_PPV_ARGS(proc.ReleaseAndGetAddressOf()));

            VARIANT vName; VariantInit(&vName);
            VARIANT vPid;  VariantInit(&vPid);

            if (proc) {
                // get Name
                if (SUCCEEDED(proc->Get(L"Name", 0, &vName, nullptr, nullptr)) &&
                    vName.vt == VT_BSTR && vName.bstrVal &&
                    _wcsicmp(vName.bstrVal, exeName_.c_str()) == 0) {

                    // get PID and check ignore list
                    if (SUCCEEDED(proc->Get(L"ProcessId", 0, &vPid, nullptr, nullptr)) &&
                        vPid.vt == VT_I4) {
                        DWORD pid = static_cast<DWORD>(vPid.intVal);
                        if (isIgnored(pid)) {
                            // Drop this event completely
                            VariantClear(&vPid);
                            VariantClear(&vName);
                            VariantClear(&vt);
                            return true;
                        }
                    }

                    // Only count if not ignored
                    if (isCreation) onCreation_();
                    else            onDeletion_();
                }
            }

            VariantClear(&vPid);
            VariantClear(&vName);
        }
        VariantClear(&vt);
        return true;
    }

    void Watchdog::drainPending_() {
        auto drainOne = [](ComPtr<IEnumWbemClassObject>& en) {
            if (!en) return false;
            bool any = false;
            for (;;) {
                ComPtr<IWbemClassObject> ev;
                ULONG ret = 0;
                // Non-blocking: 0ms timeout
                HRESULT hr = en->Next(0, 1, ev.ReleaseAndGetAddressOf(), &ret);
                if (hr != WBEM_S_NO_ERROR || ret == 0) break;
                any = true;
            }
            return any;
            };

        // Keep draining until both are empty in the same iteration
        while (drainOne(startEnum_) | drainOne(stopEnum_)) {
            // loop
        }
    }

    void Watchdog::rebaseline_() {
        // Recompute current count via one-shot query
        int cnt = 0;
        if (services_) {
            wchar_t query[256];
            swprintf_s(query, L"SELECT ProcessId FROM Win32_Process WHERE Name='%s'", exeName_.c_str());

            ComPtr<IEnumWbemClassObject> en;
            HRESULT hr = services_->ExecQuery(
                _bstr_t(L"WQL"), _bstr_t(query),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                nullptr,
                en.ReleaseAndGetAddressOf()
            );
            if (SUCCEEDED(hr)) {
                setProxy(en.Get());
                ULONG ret = 0;
                ComPtr<IWbemClassObject> obj;
                while (en->Next(0, 1, obj.ReleaseAndGetAddressOf(), &ret) == WBEM_S_NO_ERROR && ret == 1) {
                    ++cnt;
                }
            }
        }

        procCount_.store(cnt, std::memory_order_release);
        // Arm only if we *currently* have at least one instance
        armed_.store(cnt > 0, std::memory_order_release);
    }

    void Watchdog::onCreation_() {
        int before = procCount_.fetch_add(1, std::memory_order_acq_rel);
        if (before == 0) {
            armed_.store(true, std::memory_order_release);
        }
    }

    void Watchdog::onDeletion_() {
        int cur = procCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (cur < 0) { procCount_.store(0, std::memory_order_release); cur = 0; }
        if (cur == 0) {
            bool wasArmed = armed_.exchange(false, std::memory_order_acq_rel);
            if (wasArmed && onAllExited_) {
                try { onAllExited_(); }
                catch (...) {}
            }
        }
    }

    void Watchdog::addIgnoredPid(DWORD pid) {
        std::scoped_lock lk(ignoreMutex_);
        ignorePids_.insert(pid);
    }

    bool Watchdog::isIgnored(DWORD pid) {
        std::scoped_lock lk(ignoreMutex_);
        return ignorePids_.count(pid) > 0;
    }
} // namespace TITAN