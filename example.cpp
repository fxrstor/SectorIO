// Poorly written example but you get the idea.
#include <windows.h>
#include <stdio.h>

#define IOCTL_SECTOR_READ        CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_SECTOR_WRITE       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_GET_SECTOR_SIZE    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)

#pragma pack(push, 1)
typedef struct _DISK_LOCATION {
    ULONG diskIndex;
    ULONGLONG sectorNumber;
} DISK_LOCATION, *PDISK_LOCATION;
#pragma pack(pop)

void PrintHex(const UCHAR* buf, size_t length) {
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


ULONG GetSectorSize(HANDLE hDevice) {
    DISK_LOCATION getSizeLoc;
    getSizeLoc.diskIndex = 0;
    getSizeLoc.sectorNumber = 0;

    BYTE getSizeBuf[sizeof(DISK_LOCATION)] = {};
    memcpy(getSizeBuf, &getSizeLoc, sizeof(DISK_LOCATION));

    DWORD bytesReturned = 0;
    BOOL  ok = DeviceIoControl(
        hDevice,
        IOCTL_GET_SECTOR_SIZE,
        getSizeBuf,
        sizeof(getSizeBuf),
        getSizeBuf,
        sizeof(getSizeBuf),
        &bytesReturned,
        NULL
    );

    if (!ok) {
        printf("Error: IOCTL_GET_SECTOR_SIZE failed (GetLastError=%lu)\n", GetLastError());
        CloseHandle(hDevice);
        return 1;
    }
    if (bytesReturned < sizeof(ULONG)) {
        printf("WARNING: GET_SECTOR_SIZE returned %lu bytes (expected >= %zu)\n",
            bytesReturned, sizeof(ULONG));
    }

    ULONG sectorSize = *(ULONG*)getSizeBuf;
    return sectorSize; // mostly 512 bytes, sometimes 4096 bytes. See https://en.wikipedia.org/wiki/Advanced_Format
}

void PrintSectors(HANDLE hDevice, ULONG diskIndex, ULONGLONG sectorNumber, ULONG sectorSize = 512, ULONG nSectors = 1) {
    DISK_LOCATION readLoc = {};
    readLoc.diskIndex = diskIndex;
    readLoc.sectorNumber = sectorNumber;

    BYTE readInBuf[sizeof(DISK_LOCATION)] = {};
    memcpy(readInBuf, &readLoc, sizeof(DISK_LOCATION));

    UCHAR* readOutBuf = (UCHAR*)malloc(sectorSize * nSectors);
    if (!readOutBuf) {
        printf("Error: Out of memory\n");
        CloseHandle(hDevice);
        return;
    }
    ZeroMemory(readOutBuf, sectorSize * nSectors);

    ULONG bytesReturned = 0;
    BOOL ok = DeviceIoControl(
        hDevice,
        IOCTL_SECTOR_READ,
        readInBuf,
        sizeof(DISK_LOCATION),
        readOutBuf,
        sectorSize * nSectors,
        &bytesReturned,
        NULL
    );
    if (!ok) {
        printf("Error: IOCTL_SECTOR_READ(1 sector) failed (GetLastError=%lu)\n", GetLastError());
        free(readOutBuf);
        CloseHandle(hDevice);
        return;
    }
    if (bytesReturned < (sectorSize * nSectors)) {
        printf("WARNING: READ returned %lu bytes (expected %u)\n", bytesReturned, sectorSize * nSectors);
    }

    printf("Contents of sector %llu:\n", (unsigned long long)sectorNumber);
    PrintHex(readOutBuf, sectorSize * nSectors);
    free(readOutBuf);
}

void DestroySectors(HANDLE hDevice, ULONG diskIndex, ULONGLONG sectorNumber, ULONG sectorSize = 512, ULONG nSectors = 1) {
    UCHAR* writeBuf = (UCHAR*)malloc(nSectors * sectorSize);
    if (!writeBuf) {
        printf("Error: Out of memory\n");
        CloseHandle(hDevice);
        return;
    }

    DISK_LOCATION writeLoc = {};
    writeLoc.diskIndex = diskIndex;
    writeLoc.sectorNumber = sectorNumber;

    for (size_t i = 0; i < (size_t)nSectors * sectorSize; i++) {
        switch (i & 3) {
        case 0: writeBuf[i] = 0xAB; break;
        case 1: writeBuf[i] = 0xCD; break;
        case 2: writeBuf[i] = 0xEF; break;
        default: writeBuf[i] = 0x01; break;
        }
    }

    ULONG bytesReturned = 0;
    BOOL ok = DeviceIoControl(
        hDevice,
        IOCTL_SECTOR_WRITE,
        &writeLoc,
        sizeof(writeLoc),
        writeBuf,
        nSectors * sectorSize,
        &bytesReturned,
        NULL
    );
    if (!ok) {
        printf("Error: IOCTL_SECTOR_WRITE(5 sectors) failed (GetLastError=%lu)\n", GetLastError());
        free(writeBuf);
        CloseHandle(hDevice);
        return;
    }
    printf("\nWrote %u sectors (from %llu to %llu) with gibberish.\n\n", nSectors, (unsigned long long)sectorNumber, (unsigned long long)(sectorNumber + nSectors - 1));
    free(writeBuf);
}

int main()
{
    HANDLE hDevice = CreateFileW(
        L"\\\\.\\SectorIO",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Error: Cannot open \\\\.\\SectorIO  (GetLastError=%lu)\n", GetLastError());
        return 1;
    }
    printf("Opened \\\\.\\SectorIO successfully.\n\n");

    ULONG sectorSize = GetSectorSize(hDevice);
    printf("Driver reports sector size = %u bytes\n\n", sectorSize);

    PrintSectors(hDevice, 0, 0, sectorSize, 2);
    printf("Press any key to trash (write with gibberish) 15 sectors starting from 0\n");
    system("pause");
    DestroySectors(hDevice, 0, 0, sectorSize, 15);
    PrintSectors(hDevice, 0, 64000, sectorSize, 2);
    printf("Press any key to trash 5 sectors starting from 64000\n");
    system("pause");
    DestroySectors(hDevice, 0, 64000, sectorSize, 5);
    printf("Trashed...\n");
    system("pause");
    system("cls");

    CloseHandle(hDevice);
    return 0;
}
