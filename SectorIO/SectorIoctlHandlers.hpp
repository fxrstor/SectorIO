#pragma once
#include "Sector.hpp"

typedef struct _IOCTL_COMPLETION_CONTEXT {
    KEVENT event;
    IO_STATUS_BLOCK ioStatusBlock;
} IOCTL_COMPLETION_CONTEXT, * PIOCTL_COMPLETION_CONTEXT;

NTSTATUS GetSectorSizeIoctlHandler(IN PIRP pIrp, IN PSTORAGE_OBJECT pStorageObject);
NTSTATUS ReadSectorIoctlHandler(IN PIRP pIrp, IN PIO_STACK_LOCATION pIrpStack, IN PSTORAGE_OBJECT pStorageObject, IN PSTORAGE_LOCATION pStorageLocation);
NTSTATUS WriteSectorIoctlHandler(IN PIRP pIrp, IN PIO_STACK_LOCATION pIrpStack, IN PSTORAGE_OBJECT pStorageObject, IN PSTORAGE_LOCATION pStorageLocation);
NTSTATUS StorageInfoIoctlHandler(IN PIRP pIrp, IN PIO_STACK_LOCATION pIrpStack);
