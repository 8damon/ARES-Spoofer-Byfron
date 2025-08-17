// NOT IN USE, COM CONFLICT

#include "../Header/Mask.h"

#include <iostream>

namespace Mask {
    bool Masker::IsValidReadPtr(const void* ptr, SIZE_T size) {
        if (!ptr || size == 0) return false;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        if (mbi.State != MEM_COMMIT || mbi.Protect == PAGE_NOACCESS) return false;
        if ((uintptr_t)ptr + size > (uintptr_t)mbi.BaseAddress + mbi.RegionSize) return false;

        return (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    }

    bool Masker::IsValidWritePtr(void* ptr, SIZE_T size) {
        if (!ptr || size == 0) return false;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        if (mbi.State != MEM_COMMIT || mbi.Protect == PAGE_NOACCESS) return false;
        if ((uintptr_t)ptr + size > (uintptr_t)mbi.BaseAddress + mbi.RegionSize) return false;

        return (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    }

    PPEB Masker::GetPEB() { return reinterpret_cast<PPEB>(__readgsqword(0x60));}

    void Masker::SetUnicodeString(PUNICODE_STRING us, const std::wstring& str, PWSTR* oldBuffer) {
        if (!us || !IsValidWritePtr(us, sizeof(UNICODE_STRING))) return;

        SIZE_T allocSize = (str.length() + 1) * sizeof(wchar_t);
        PWSTR newBuffer = static_cast<PWSTR>(VirtualAlloc(nullptr, allocSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!newBuffer) return;

        memcpy(newBuffer, str.data(), allocSize - sizeof(wchar_t));

        if (oldBuffer && us->Buffer && IsValidReadPtr(us->Buffer, us->MaximumLength)) {
            *oldBuffer = us->Buffer;
        }

        us->Buffer = newBuffer;
        us->Length = static_cast<USHORT>(str.length() * sizeof(wchar_t));
        us->MaximumLength = static_cast<USHORT>(allocSize);
    }

    void Masker::run() {
        PPEB peb = GetPEB();
        if (!peb || !IsValidReadPtr(peb, sizeof(PEB))) return;

        PRTL_USER_PROCESS_PARAMETERS params = peb->ProcessParameters;
        if (!params || !IsValidReadPtr(params, sizeof(RTL_USER_PROCESS_PARAMETERS))) return;

        std::wstring spoofPath = L"C:\\Windows\\System32\\tsspf.exe";
        std::wstring spoofCmd = L"tsspf.exe";
        std::wstring spoofDir = L"C:\\Windows\\System32";

        PWSTR oldImg = nullptr, oldCmd = nullptr, oldDir = nullptr;

        SetUnicodeString(&params->ImagePathName, spoofPath, &oldImg);
        SetUnicodeString(&params->CommandLine, spoofCmd, &oldCmd);
        SetUnicodeString(&params->CurrentDirectory.DosPath, spoofDir, &oldDir);

        if (IsValidWritePtr(&params->ConsoleHandle, sizeof(HANDLE))) {
            HANDLE invalid = INVALID_HANDLE_VALUE;
            SIZE_T bytesWritten;
            WriteProcessMemory(GetCurrentProcess(), &params->ConsoleHandle, &invalid, sizeof(HANDLE), &bytesWritten);
        }

        auto cleanup = [](PWSTR buf, USHORT maxLen) { if (buf && maxLen > 0 && VirtualFree(buf, 0, MEM_RELEASE)) {} };

        if (oldImg) cleanup(oldImg, params->ImagePathName.MaximumLength);
        if (oldCmd) cleanup(oldCmd, params->CommandLine.MaximumLength);
        if (oldDir) cleanup(oldDir, params->CurrentDirectory.DosPath.MaximumLength);

        HMODULE base = GetModuleHandle(nullptr);
        if (!base) return;

        IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;

        IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<BYTE*>(base) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return;

        DWORD oldProt;
        VirtualProtect(base, 0x1000, PAGE_READWRITE, &oldProt);

        nt->FileHeader.TimeDateStamp = 0;
        nt->OptionalHeader.CheckSum = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].VirtualAddress = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress = 0;
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size = 0;

        VirtualProtect(base, 0x1000, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), base, 0x1000);

        IMAGE_DATA_DIRECTORY& dbgDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        if (dbgDir.VirtualAddress && dbgDir.Size >= sizeof(IMAGE_DEBUG_DIRECTORY)) {
            BYTE* dbgPtr = reinterpret_cast<BYTE*>(base) + dbgDir.VirtualAddress;
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            PVOID regionBase = reinterpret_cast<PVOID>((reinterpret_cast<ULONG_PTR>(dbgPtr) & ~(si.dwPageSize - 1)));
            SIZE_T regionSize = dbgDir.Size + (reinterpret_cast<ULONG_PTR>(dbgPtr) - reinterpret_cast<ULONG_PTR>(regionBase));

            VirtualProtect(regionBase, regionSize, PAGE_READWRITE, &oldProt);
            memset(dbgPtr, 0, dbgDir.Size);
            VirtualProtect(regionBase, regionSize, oldProt, &oldProt);
            FlushInstructionCache(GetCurrentProcess(), regionBase, regionSize);
        }
    }
}