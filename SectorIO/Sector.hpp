#pragma once
#include <ntddk.h>
#include <ntdddisk.h>
#include <ntstrsafe.h>
#include "Main.hpp"
#include "vector.hpp"

extern "C" {
    NTSYSAPI NTSTATUS NTAPI ObReferenceObjectByName(
        PUNICODE_STRING ObjectName,
        ULONG Attributes,
        PACCESS_STATE AccessState,
        ACCESS_MASK DesiredAccess,
        POBJECT_TYPE ObjectType,
        KPROCESSOR_MODE AccessMode,
        PVOID ParseContext OPTIONAL,
        PVOID* Object
    );

    NTSYSAPI NTSTATUS NTAPI ObQueryNameString(
        PVOID Object,
        POBJECT_NAME_INFORMATION ObjectNameInfo,
        ULONG Length,
        PULONG ReturnLength
    );

    NTSYSAPI POBJECT_TYPE* IoDriverObjectType;
}

// Grabbed from phnt project - ntobapi.h
typedef struct _OBJECT_DIRECTORY_INFORMATION
{
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION, * POBJECT_DIRECTORY_INFORMATION;

typedef struct _IOCTL_COMPLETION_CONTEXT {
    KEVENT event;
    IO_STATUS_BLOCK ioStatusBlock;
} IOCTL_COMPLETION_CONTEXT, * PIOCTL_COMPLETION_CONTEXT;


#pragma pack (push, 1)

typedef struct _DISK_OBJECT {
    BOOLEAN geometryFound;
    ULONG diskIndex;
    ULONG sectorSize;
    PDEVICE_OBJECT pDiskDeviceObject;
} DISK_OBJECT, *PDISK_OBJECT;

typedef struct _DISK_LOCATION {
    ULONG diskIndex;
    ULONGLONG sectorNumber;
} DISK_LOCATION, *PDISK_LOCATION;

#pragma pack (pop)

NTSTATUS GetAllDiskObjects();
NTSTATUS GetGeometry(PDEVICE_OBJECT pDiskDeviceObject, PDISK_GEOMETRY pDiskGeometry);

NTSTATUS GetSectorSizeIoctlHandler(IN PIRP pIrp, IN PDISK_OBJECT pDiskObject);
NTSTATUS RWIrpCompletion(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PVOID Context);
NTSTATUS ReadSectorIoctlHandler(IN PIRP pIrp, IN PIO_STACK_LOCATION pIrpStack, IN PDISK_OBJECT pDiskObject, IN PDISK_LOCATION pDiskLocation);
NTSTATUS WriteSectorIoctlHandler(IN PIRP pIrp, IN PIO_STACK_LOCATION pIrpStack, IN PDISK_OBJECT pDiskObject, IN PDISK_LOCATION pDiskLocation);

extern vector<PDISK_OBJECT>* g_pDiskObjects;