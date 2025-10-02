#pragma once
#include <initguid.h>
#include "Driver.hpp"
#include "vector.hpp"
#include "DeviceIo.hpp"

#pragma pack (push, 1)

typedef struct _STORAGE_OBJECT_INFO {
    BOOLEAN isRawDiskObject;

    ULONG diskIndex;
    ULONG partitionNumber;

    ULONGLONG partitionStartingOffset;
    ULONGLONG partitionSizeBytes;
    ULONGLONG diskSizeBytes;

    // ULONGLONG volumeTotalBytes;
    // ULONGLONG volumeFreeBytes;

    ULONG sectorSize;

    // GUID volumeGuid;
    GUID gptDiskId;

    PARTITION_STYLE partitionStyle;
    GUID gptPartitionTypeGuid;
    GUID gptPartitionIdGuid;
    ULONGLONG gptAttributes;
    WCHAR gptName[36];
    UCHAR mbrPartitionType;

    // ULONG volumeSerialNumber;
    // WCHAR volumeLabel[64];
    // WCHAR fileSystemName[32];
    // WCHAR driveLetters[8*2];

    // TODO: implement the commented stuff...
} STORAGE_OBJECT_INFO, *PSTORAGE_OBJECT_INFO;

typedef struct _STORAGE_OBJECT {
    STORAGE_OBJECT_INFO info;
    PDEVICE_OBJECT pStorageDeviceObject;
} STORAGE_OBJECT, *PSTORAGE_OBJECT;

typedef struct _STORAGE_LOCATION {
    BOOLEAN isRawDiskObject;
    ULONG diskIndex;
    ULONG partitionNumber;
    ULONGLONG sectorNumber;
} STORAGE_LOCATION, *PSTORAGE_LOCATION;

#pragma pack (pop)

void FreeCollectedStorageObjects();
NTSTATUS RefreshGlobalStorageObjects();

extern vector<PSTORAGE_OBJECT>* g_pStorageObjects;
