#include "../Header/COM.h"

namespace COM {

    COMInitializer::COMInitializer() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            if (hr != RPC_E_CHANGED_MODE) {
                throw std::runtime_error("COM init failed");
            }
        }

        hr = CoInitializeSecurity(
            nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr, EOAC_NONE, nullptr
        );

        if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
            CoUninitialize();
            throw std::runtime_error("COM security init failed");
        }
    }

    COMInitializer::~COMInitializer() {
        CoUninitialize();
    }

    bool COMInitializer::initializeWMI(IWbemLocator*& pLocator, IWbemServices*& pService) {
        HRESULT hr = CoCreateInstance(
            CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
            reinterpret_cast<LPVOID*>(&pLocator)
        );
        if (FAILED(hr)) {
            return false;
        }

        BSTR resourcePath = SysAllocString(L"ROOT\\CIMV2");
        hr = pLocator->ConnectServer(
            resourcePath, nullptr, nullptr, 0, NULL, 0, 0, &pService
        );
        SysFreeString(resourcePath);

        if (FAILED(hr)) {
            pLocator->Release();
            return false;
        }

        hr = CoSetProxyBlanket(
            pService, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE
        );
        if (FAILED(hr)) {
            pService->Release();
            pLocator->Release();
            return false;
        }

        return true;
    }
}