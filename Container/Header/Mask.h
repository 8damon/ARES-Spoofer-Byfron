#pragma once
#include <windows.h>
#include <string>

typedef LONG NTSTATUS;
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

typedef struct _TS_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} TS_UNICODE_STRING, * PTS_UNICODE_STRING;

typedef struct _TS_CURDIR {
    TS_UNICODE_STRING DosPath;
    HANDLE Handle;
} TS_CURDIR, * PTS_CURDIR;

typedef struct _TS_RTL_USER_PROCESS_PARAMETERS {
    ULONG MaximumLength;
    ULONG Length;
    ULONG Flags;
    ULONG DebugFlags;
    HANDLE ConsoleHandle;
    ULONG ConsoleFlags;
    HANDLE StandardInput;
    HANDLE StandardOutput;
    HANDLE StandardError;
    TS_CURDIR CurrentDirectory;
    TS_UNICODE_STRING DllPath;
    TS_UNICODE_STRING ImagePathName;
    TS_UNICODE_STRING CommandLine;
} TS_RTL_USER_PROCESS_PARAMETERS, * PTS_RTL_USER_PROCESS_PARAMETERS;

typedef struct _TS_PEB {
    BYTE Reserved1[2];
    BYTE BeingDebugged;
    BYTE Reserved2[1];
    PVOID Reserved3[2];
    PTS_RTL_USER_PROCESS_PARAMETERS ProcessParameters;
} TS_PEB, * PTS_PEB;

typedef struct _TS_IMAGE_DEBUG_DIRECTORY {
    DWORD Characteristics;
    DWORD TimeDateStamp;
    WORD MajorVersion;
    WORD MinorVersion;
    DWORD Type;
    DWORD SizeOfData;
    DWORD AddressOfRawData;
    DWORD PointerToRawData;
} TS_IMAGE_DEBUG_DIRECTORY, * PTS_IMAGE_DEBUG_DIRECTORY;

namespace Mask {
    class Masker {
    public:
        static void run();
    private:
        static PTS_PEB GetPEB();
        static bool IsValidReadPtr(const void* ptr, SIZE_T size);
        static bool IsValidWritePtr(void* ptr, SIZE_T size);
        static void SetUnicodeString(PTS_UNICODE_STRING us, const std::wstring& str, PWSTR* oldBuffer = nullptr);
    };
}