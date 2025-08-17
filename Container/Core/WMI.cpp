#include "../Header/WMI.h"

namespace WMI {

    void WmiSpoofer::run() {
        TsService::SectHeader("WMI Spoofing", 90);

        if (!SystemProduct()) {
            std::cerr << "Win32_ComputerSystemProduct Spoof failed" << std::endl;
        }

        if (!PhysMem()) {
            std::cerr << "Win32_PhysicalMemory Spoof failed" << std::endl;
        }
    }

    bool WmiSpoofer::SystemProduct() {

        COM::COMInitializer comInit; // only 1

        IWbemLocator* pLocator = nullptr;
        IWbemServices* pService = nullptr;

        if (!comInit.initializeWMI(pLocator, pService)) {
            return false;
        }

        const BSTR queryLang = SysAllocString(L"WQL");
        const BSTR queryStr = SysAllocString(L"SELECT * FROM Win32_ComputerSystemProduct");

        IEnumWbemClassObject* pEnumerator = nullptr;
        HRESULT hr = pService->ExecQuery(
            queryLang, queryStr,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &pEnumerator
        );

        SysFreeString(queryLang);
        SysFreeString(queryStr);

        if (FAILED(hr)) {
            if (pService) pService->Release();
            if (pLocator) pLocator->Release();
            return false;
        }

        IWbemClassObject* pObject = nullptr;
        ULONG uReturn = 0;

        std::wstring newUUID = TsService::stringToWString(TsService::genGUID());

        while (pEnumerator->Next(WBEM_INFINITE, 1, &pObject, &uReturn) == WBEM_S_NO_ERROR) {
            VARIANT varValue;
            VariantInit(&varValue);
            V_VT(&varValue) = VT_BSTR;
            V_BSTR(&varValue) = SysAllocString(newUUID.c_str());

            hr = pObject->Put(L"UUID", 0, &varValue, 0);
            VariantClear(&varValue);

            if (FAILED(hr)) {
                if (pObject) pObject->Release();
                if (pEnumerator) pEnumerator->Release();
                if (pService) pService->Release();
                if (pLocator) pLocator->Release();
                return false;
            }

            std::wcout << L"Spoofed -> Win32_ComputerSystemProduct, New UUID -> " << newUUID << std::endl;
            if (pObject) pObject->Release();
        }

        if (pEnumerator) pEnumerator->Release();
        if (pService) pService->Release();
        if (pLocator) pLocator->Release();
        return true;
    }

    bool WmiSpoofer::PhysMem() {
        COM::COMInitializer comInit;

        IWbemLocator* pLocator = nullptr;
        IWbemServices* pService = nullptr;

        if (!comInit.initializeWMI(pLocator, pService)) {
            return false;
        }

        const BSTR queryLang = SysAllocString(L"WQL");
        const BSTR queryStr = SysAllocString(L"SELECT * FROM Win32_PhysicalMemory");

        IEnumWbemClassObject* pEnumerator = nullptr;
        HRESULT hr = pService->ExecQuery(
            queryLang, queryStr,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &pEnumerator
        );

        SysFreeString(queryLang);
        SysFreeString(queryStr);

        if (FAILED(hr)) {
            if (pService) pService->Release();
            if (pLocator) pLocator->Release();
            return false;
        }

        IWbemClassObject* pObject = nullptr;
        ULONG uReturn = 0;

        std::wstring newSerial = TsService::stringToWString(TsService::genSerial());

        while (pEnumerator->Next(WBEM_INFINITE, 1, &pObject, &uReturn) == WBEM_S_NO_ERROR) {
            VARIANT varValue;
            VariantInit(&varValue);
            V_VT(&varValue) = VT_BSTR;
            V_BSTR(&varValue) = SysAllocString(newSerial.c_str());

            hr = pObject->Put(L"SerialNumber", 0, &varValue, 0);
            VariantClear(&varValue);

            if (FAILED(hr)) {
                if (pObject) pObject->Release();
                if (pEnumerator) pEnumerator->Release();
                if (pService) pService->Release();
                if (pLocator) pLocator->Release();
                return false;
            }

            std::wcout << L"Spoofed -> Win32_PhysicalMemory, New serial -> " << newSerial << std::endl;
            if (pObject) pObject->Release();
        }

        if (pEnumerator) pEnumerator->Release();
        if (pService) pService->Release();
        if (pLocator) pLocator->Release();

        return true;
    }

}