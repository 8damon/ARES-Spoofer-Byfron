#pragma once

#ifndef DEFS_H
#define DEFS_H

#include <Windows.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

#ifndef OBJ_CASE_INSENSITIVE
#define OBJ_CASE_INSENSITIVE 0x00000040L
#endif

#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif

typedef NTSTATUS(NTAPI* pNtUnmapViewOfSection)(HANDLE ProcessHandle, PVOID BaseAddress);

#ifndef _UNICODE_STRING_DEFINED
#define _UNICODE_STRING_DEFINED
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING, * PUNICODE_STRING;
#endif

#ifndef _OBJECT_ATTRIBUTES_DEFINED
#define _OBJECT_ATTRIBUTES_DEFINED
typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, * POBJECT_ATTRIBUTES;
#endif

typedef enum _KEY_INFORMATION_CLASS {
    KeyBasicInformation = 0,
    KeyNodeInformation = 1,
    KeyFullInformation = 2,
} KEY_INFORMATION_CLASS;

#ifndef _KEY_FULL_INFORMATION_DEFINED
#define _KEY_FULL_INFORMATION_DEFINED
typedef struct _KEY_FULL_INFORMATION {
    ULONG ClassOffset;
    ULONG ClassLength;
    ULONG SubKeys;
    ULONG MaxNameLen;
    ULONG Values;
    ULONG MaxValueNameLen;
    ULONG MaxValueDataLen;
} KEY_FULL_INFORMATION, * PKEY_FULL_INFORMATION;
#endif

#ifndef InitializeObjectAttributes
#define InitializeObjectAttributes(p, n, a, r, s) { \
    (p)->Length = sizeof(OBJECT_ATTRIBUTES);       \
    (p)->RootDirectory = r;                        \
    (p)->Attributes = a;                           \
    (p)->ObjectName = n;                           \
    (p)->SecurityDescriptor = s;                   \
    (p)->SecurityQualityOfService = nullptr;       \
}
#endif

using RtlInitUnicodeString_t = void(NTAPI*)(PUNICODE_STRING, PCWSTR);

inline void RtlInitUnicodeString(PUNICODE_STRING DestinationString, PCWSTR SourceString) {
    if (!DestinationString) return;

    size_t length = SourceString ? wcslen(SourceString) * sizeof(WCHAR) : 0;

    DestinationString->Length = static_cast<USHORT>(length);
    DestinationString->MaximumLength = static_cast<USHORT>(length + sizeof(WCHAR));
    DestinationString->Buffer = const_cast<PWSTR>(SourceString);
}

typedef enum _KEY_VALUE_INFORMATION_CLASS {
    KeyValueBasicInformation = 0,
    KeyValueFullInformation = 1,
    KeyValuePartialInformation = 2,
} KEY_VALUE_INFORMATION_CLASS;

#ifndef _KEY_VALUE_FULL_INFORMATION_DEFINED
#define _KEY_VALUE_FULL_INFORMATION_DEFINED
typedef struct _KEY_VALUE_FULL_INFORMATION {
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataOffset;
    ULONG DataLength;
    ULONG NameLength;
    WCHAR Name[1];
} KEY_VALUE_FULL_INFORMATION, * PKEY_VALUE_FULL_INFORMATION;
#endif

using NtOpenKey_t = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
using NtClose_t = NTSTATUS(NTAPI*)(HANDLE);
using NtQueryKey_t = NTSTATUS(NTAPI*)(HANDLE, KEY_INFORMATION_CLASS, PVOID, ULONG, PULONG);
using NtQueryValueKey_t = NTSTATUS(NTAPI*)(HANDLE, PUNICODE_STRING, KEY_VALUE_INFORMATION_CLASS, PVOID, ULONG, PULONG);
using NtSetValueKey_t = NTSTATUS(NTAPI*)(HANDLE, PUNICODE_STRING, ULONG, ULONG, const void*, ULONG);
using NtEnumerateKey_t = NTSTATUS(NTAPI*)(HANDLE, ULONG, KEY_INFORMATION_CLASS, PVOID, ULONG, PULONG);

#endif // DEFS_H