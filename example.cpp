// This program requires should be ran with administrative privileges only
// Otherwise the handle to C: disk will not opened
// Furthermore, if secure device macro is enabled in driver,
// Opening driver handle will fail.
#include <windows.h>
#include <stdio.h>

#define SECTOR_IO_DEVICE         0x8000
#define IOCTL_SECTOR_READ        CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_SECTOR_WRITE       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_GET_SECTOR_SIZE    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_GET_STORAGE_INFO   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_NEITHER, FILE_ANY_ACCESS)

#pragma pack(push, 1)
typedef struct _STORAGE_LOCATION {
    BOOLEAN isRawDiskObject;
    ULONG diskIndex;
    ULONG partitionNumber;
    ULONGLONG sectorNumber;
} STORAGE_LOCATION, * PSTORAGE_LOCATION;

typedef struct _STORAGE_OBJECT_INFO {
    BOOLEAN isRawDiskObject;

    ULONG diskIndex;
    ULONG partitionNumber;

    ULONGLONG partitionStartingOffset;
    ULONGLONG partitionSizeBytes;
    ULONGLONG diskSizeBytes;

    ULONG sectorSize;

    GUID gptDiskId;

    PARTITION_STYLE partitionStyle;
    GUID gptPartitionTypeGuid;
    GUID gptPartitionIdGuid;
    ULONGLONG gptAttributes;
    WCHAR gptName[36];
    UCHAR mbrPartitionType;
} STORAGE_OBJECT_INFO, * PSTORAGE_OBJECT_INFO;
#pragma pack(pop)

static void PrintGuid(const GUID* guid) {
    printf("%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
        guid->Data1, guid->Data2, guid->Data3,
        guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
        guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
}

static void PrintWideString(const WCHAR* wstr, size_t maxLen) {
    for (size_t i = 0; i < maxLen; ++i) {
        if (wstr[i] == 0) break;
        putchar((char)wstr[i]); 
    }
}

static void PrintStorageObjectInfo(const STORAGE_OBJECT_INFO* info) {
    printf("  isRawDiskObject: %s\n", info->isRawDiskObject ? "TRUE" : "FALSE");
    printf("  diskIndex: %lu\n", info->diskIndex);
    printf("  partitionNumber: %lu\n", info->partitionNumber);
    printf("  partitionStartingOffset: %llu\n", (unsigned long long)info->partitionStartingOffset);
    printf("  partitionSizeBytes: %llu\n", (unsigned long long)info->partitionSizeBytes);
    printf("  diskSizeBytes: %llu\n", (unsigned long long)info->diskSizeBytes);
    printf("  sectorSize: %lu\n", info->sectorSize);
    printf("  gptDiskId: ");
    PrintGuid(&info->gptDiskId);
    printf("\n");
    printf("  partitionStyle: %s\n",
        (info->partitionStyle == PARTITION_STYLE_MBR) ? "MBR" :
        (info->partitionStyle == PARTITION_STYLE_GPT) ? "GPT" : "RAW");
    printf("  gptPartitionTypeGuid: ");
    PrintGuid(&info->gptPartitionTypeGuid);
    printf("\n");
    printf("  gptPartitionIdGuid: ");
    PrintGuid(&info->gptPartitionIdGuid);
    printf("\n");
    printf("  gptAttributes: %llu\n", (unsigned long long)info->gptAttributes);
    printf("  gptName: ");
    PrintWideString(info->gptName, 36);
    printf("\n");
    printf("  mbrPartitionType: 0x%02X\n", info->mbrPartitionType);
}
static void FetchAndPrintStorageInfo(HANDLE hDevice) {
    SIZE_T requiredBytes = 0;
    DWORD bytesReturned = 0;
    BOOL ok;

    bytesReturned = 0;
    ok = DeviceIoControl(hDevice, IOCTL_GET_STORAGE_INFO, NULL, 0, &requiredBytes, (DWORD)sizeof(requiredBytes), &bytesReturned, NULL);

    DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    printf("DeviceIoControl(query size) -> ok=%d err=%u bytesReturned=%u requiredBytes(after call)=%llu\n", ok, err, bytesReturned, (unsigned long long)requiredBytes);

    if (!ok && err != ERROR_INSUFFICIENT_BUFFER && err != ERROR_MORE_DATA) {
        printf("Error: IOCTL_GET_STORAGE_INFO (query size) failed. GetLastError=%lu bytesReturned=%u\n", err, bytesReturned);
        return;
    }

    if (requiredBytes == 0) {
        printf("Error: driver did not return required size (requiredBytes==0). bytesReturned=%u err=%u\n", bytesReturned, err);
        return;
    }

    if (requiredBytes == 0) {
        printf("No storage objects available.\n");
        return;
    }

    DWORD outBufferLen = (DWORD)requiredBytes;
    void* buffer = malloc(outBufferLen);
    if (!buffer) {
        printf("Error: could not allocate %u bytes\n", outBufferLen);
        return;
    }

    bytesReturned = 0;
    ok = DeviceIoControl(hDevice, IOCTL_GET_STORAGE_INFO, NULL, 0, buffer, outBufferLen, &bytesReturned, NULL);

    if (!ok) {
        err = GetLastError();
        printf("Error: IOCTL_GET_STORAGE_INFO (retrieve) failed. GetLastError=%lu bytesReturned=%u\n", err, bytesReturned);
        free(buffer);
        return;
    }

    if (bytesReturned == 0) {
        printf("Error: driver returned zero bytes on retrieval\n");
        free(buffer);
        return;
    }

    if (bytesReturned % sizeof(STORAGE_OBJECT_INFO) != 0) {
        printf("Warning: returned size %u is not a multiple of STORAGE_OBJECT_INFO (%zu)\n", bytesReturned, sizeof(STORAGE_OBJECT_INFO));
    }

    ULONG count = (ULONG)(bytesReturned / sizeof(STORAGE_OBJECT_INFO));
    printf("Fetched %lu storage objects (bytesReturned=%u):\n", count, bytesReturned);

    STORAGE_OBJECT_INFO* infos = (STORAGE_OBJECT_INFO*)buffer;
    for (ULONG i = 0; i < count; ++i) {
        printf("Storage Object #%lu:\n", i);
        PrintStorageObjectInfo(&infos[i]);
        printf("\n");
    }

    free(buffer);
}

static void PrintHex(const UCHAR* buf, size_t length)
{
    const size_t kCols = 16;
    for (size_t offs = 0; offs < length; offs += kCols) {
        printf("%08zx: ", offs);
        for (size_t i = 0; i < kCols; i++) {
            if (offs + i < length)
                printf("%02X ", buf[offs + i]);
            else
                printf("   ");
        }
        printf("  ");
        for (size_t i = 0; i < kCols; i++) {
            if (offs + i < length) {
                UCHAR c = buf[offs + i];
                putchar((c >= 0x20 && c < 0x7F) ? c : '.');
            }
        }
        printf("\n");
    }
}

ULONG GetSectorSize(HANDLE hDevice, BOOLEAN isRawDiskObject, ULONG diskIndex, ULONG partitionNumber) {
    STORAGE_LOCATION getSizeLoc = {};
    getSizeLoc.isRawDiskObject = isRawDiskObject;
    getSizeLoc.diskIndex = diskIndex;
    getSizeLoc.partitionNumber = partitionNumber;
    getSizeLoc.sectorNumber = 0;

    ULONG sectorSize = 0;
    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl( hDevice, IOCTL_GET_SECTOR_SIZE, &getSizeLoc, sizeof(STORAGE_LOCATION), &sectorSize, sizeof(ULONG), &bytesReturned, NULL);

    if (!ok) {
        printf("Error: IOCTL_GET_SECTOR_SIZE failed (GetLastError=%lu)\n", GetLastError());
        CloseHandle(hDevice);
        return 1;
    }
    if (bytesReturned != sizeof(ULONG)) {
        printf("Warning: GET_SECTOR_SIZE returned %lu bytes (expected %zu)\n", bytesReturned, sizeof(ULONG));
    }

    return sectorSize;
}

void PrintSectors(HANDLE hDevice, BOOLEAN isRawDiskObject, ULONG diskIndex, ULONG partitionNumber, ULONGLONG sectorNumber, ULONG sectorSize = 512, ULONG nSectors = 1) {
    STORAGE_LOCATION readLoc = {};
    readLoc.isRawDiskObject = isRawDiskObject;
    readLoc.diskIndex = diskIndex;
    readLoc.partitionNumber = partitionNumber;
    readLoc.sectorNumber = sectorNumber;

    UCHAR* readOutBuf = (UCHAR*)malloc(sectorSize * nSectors);
    if (!readOutBuf) {
        printf("Error: Out of memory\n");
        CloseHandle(hDevice);
        return;
    }
    ZeroMemory(readOutBuf, sectorSize * nSectors);

    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl( hDevice, IOCTL_SECTOR_READ, &readLoc, sizeof(STORAGE_LOCATION), readOutBuf, sectorSize * nSectors, &bytesReturned, NULL);
    if (!ok) {
        printf("Error: IOCTL_SECTOR_READ(%u sectors) failed (GetLastError=%lu)\n", nSectors, GetLastError());
        free(readOutBuf);
        CloseHandle(hDevice);
        return;
    }
    if (bytesReturned < (sectorSize * nSectors)) {
        printf("Warning: READ returned %lu bytes (expected %u)\n", bytesReturned, sectorSize * nSectors);
    }

    printf("Contents of sector %llu (and next %u-1 sectors):\n", (unsigned long long)sectorNumber, nSectors);
    PrintHex(readOutBuf, sectorSize * nSectors);
    free(readOutBuf);
}

// extremely risky, DO NOT run this unless you're in a vm
void DestroySectors(HANDLE hDevice, BOOLEAN isRawDiskObject, ULONG diskIndex, ULONG partitionNumber, ULONGLONG sectorNumber, ULONG sectorSize = 512, ULONG nSectors = 1) {
    UCHAR* writeBuf = (UCHAR*)malloc(nSectors * sectorSize);
    if (!writeBuf) {
        printf("Error: Out of memory\n");
        CloseHandle(hDevice);
        return;
    }

    STORAGE_LOCATION writeLoc = {};
    writeLoc.isRawDiskObject = isRawDiskObject;
    writeLoc.diskIndex = diskIndex;
    writeLoc.partitionNumber = partitionNumber;
    writeLoc.sectorNumber = sectorNumber;

    // gibberish lmao
    for (size_t i = 0; i < (size_t)nSectors * sectorSize; i++) {
        switch (i & 3) {
        case 0: writeBuf[i] = 0xAB; break;
        case 1: writeBuf[i] = 0xCD; break;
        case 2: writeBuf[i] = 0xEF; break;
        default: writeBuf[i] = 0x01; break;
        }
    }

    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(hDevice, IOCTL_SECTOR_WRITE, &writeLoc, sizeof(writeLoc), writeBuf, nSectors * sectorSize, &bytesReturned, NULL);
    if (!ok) {
        printf("Error: IOCTL_SECTOR_WRITE(%u sectors) failed (GetLastError=%lu)\n", nSectors, GetLastError());
        free(writeBuf);
        CloseHandle(hDevice);
        return;
    }

    printf("\nWrote %u sectors (from %llu to %llu) with gibberish.\n\n", nSectors, (unsigned long long)sectorNumber, (unsigned long long)(sectorNumber + nSectors - 1));
    free(writeBuf);
}

int main()
{
    HANDLE hDevice = CreateFileA("\\\\.\\SectorIO", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Error: Cannot open \\\\.\\SectorIO  (GetLastError=%lu)\n", GetLastError());
        return 1;
    }
    printf("Opened \\\\.\\SectorIO successfully.\n\n");

    printf("Fetching all storage object info:\n");
    FetchAndPrintStorageInfo(hDevice);
    printf("\n");

    printf("Testing raw disk (isRawDiskObject=TRUE, diskIndex=0, partitionNumber=0):\n");
    ULONG rawSectorSize = GetSectorSize(hDevice, TRUE, 0, 0);
    printf("Driver reports sector size = %u bytes\n\n", rawSectorSize);

    PrintSectors(hDevice, TRUE, 0, 0, 0, rawSectorSize, 2);
    printf("Press any key to trash 15 sectors starting from 0\n");
    system("pause");
    DestroySectors(hDevice, TRUE, 0, 0, 0, rawSectorSize, 15);

    PrintSectors(hDevice, TRUE, 0, 0, 64000, rawSectorSize, 2);
    printf("Press any key to kill 5 sectors starting from 64000\n");
    system("pause");
    DestroySectors(hDevice, TRUE, 0, 0, 64000, rawSectorSize, 5);
    printf("Trashed...\n");
    system("cls");

    printf("Testing C: partition (volume, isRawDiskObject=FALSE):\n");
    char volumeName[256] = { 0 };
    DWORD serialNumber = 0;
    DWORD maxComponentLength = 0;
    DWORD fileSystemFlags = 0;
    char fileSystemName[256] = { 0 };

    BOOL volInfoOk = GetVolumeInformationA("C:\\", volumeName, sizeof(volumeName), &serialNumber, &maxComponentLength, &fileSystemFlags, fileSystemName, sizeof(fileSystemName));
    if (volInfoOk) {
        printf("Volume Information for C::\n");
        printf("  Volume Name: %s\n", volumeName);
        printf("  Serial Number: 0x%08X\n", serialNumber);
        printf("  Max Component Length: %lu\n", maxComponentLength);
        printf("  File System Flags: 0x%08X\n", fileSystemFlags);
        printf("  File System Name: %s\n\n", fileSystemName);
    }
    else {
        printf("Error: Failed to fetch volume information for C: (GetLastError=%lu)\n\n", GetLastError());
    }

    ULONG volDiskIndex = 0;
    ULONG volPartitionNumber = 0;
    HANDLE hVol = CreateFileA("\\\\.\\C:", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hVol == INVALID_HANDLE_VALUE) {
        printf("Error: Cannot open \\\\.\\C: (GetLastError=%lu)\n", GetLastError());
        CloseHandle(hDevice);
        return 1;
    }

    STORAGE_DEVICE_NUMBER sdn = { 0 };
    DWORD bytesReturned = 0;
    BOOL sdnOk = DeviceIoControl(hVol, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(STORAGE_DEVICE_NUMBER), &bytesReturned, NULL);
    CloseHandle(hVol);

    if (sdnOk && sdn.DeviceType == FILE_DEVICE_DISK) {
        volDiskIndex = sdn.DeviceNumber;
        volPartitionNumber = sdn.PartitionNumber;
        printf("C: is on disk %lu, partition %lu\n\n", volDiskIndex, volPartitionNumber);
    }
    else {
        printf("Error: Failed to get storage device number for C: (GetLastError=%lu)\n", GetLastError());
        return 1;
    }

    ULONG volSectorSize = GetSectorSize(hDevice, FALSE, volDiskIndex, volPartitionNumber);
    printf("Driver reports sector size = %u bytes\n\n", volSectorSize);

    PrintSectors(hDevice, FALSE, volDiskIndex, volPartitionNumber, 0, volSectorSize, 2);
    printf("Press any key to trash 15 sectors starting from 0\n");
	system("pause");
    DestroySectors(hDevice, FALSE, volDiskIndex, volPartitionNumber, 0, volSectorSize, 15);


    PrintSectors(hDevice, FALSE, volDiskIndex, volPartitionNumber, 64000, volSectorSize, 2);
    printf("Press any key to kill 5 sectors starting from 64000\n");
    system("pause");
    DestroySectors(hDevice, FALSE, volDiskIndex, volPartitionNumber, 64000, volSectorSize, 5);
    printf("Trashed...\n");


    CloseHandle(hDevice);
    return 0;
}
