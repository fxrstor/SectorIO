#include "Driver.hpp"
#include "SectorIoctlHandlers.hpp"

#define SECTOR_IO_CTL_CODE(id) CTL_CODE(FILE_DEVICE_UNKNOWN, id, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_SECTOR_READ		SECTOR_IO_CTL_CODE(0x800)
#define IOCTL_SECTOR_WRITE		SECTOR_IO_CTL_CODE(0x801)
#define IOCTL_GET_SECTOR_SIZE	SECTOR_IO_CTL_CODE(0x802)
#define IOCTL_GET_DISK_INFO     SECTOR_IO_CTL_CODE(0x803)


NTSTATUS DriverIoDeviceDispatchRoutine(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
    UNREFERENCED_PARAMETER(pDeviceObject);
    NTSTATUS status = STATUS_SUCCESS;
    IoSetCancelRoutine(pIrp, NULL);
    PIO_STACK_LOCATION pIrpStack = IoGetCurrentIrpStackLocation(pIrp);

    LOG("DriverIoDeviceDispatchRoutine called\n");

    STORAGE_LOCATION pStorageLocation = {0};
    PSTORAGE_LOCATION pStorageLocationUser = (PSTORAGE_LOCATION)pIrpStack->Parameters.DeviceIoControl.Type3InputBuffer;

    if (pStorageLocationUser) {
        __try {
            ProbeForRead(pStorageLocationUser, sizeof(STORAGE_LOCATION), __alignof(STORAGE_LOCATION));
            RtlCopyMemory(&pStorageLocation, pStorageLocationUser, sizeof(STORAGE_LOCATION));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            status = GetExceptionCode();
            pIrp->IoStatus.Status = status;
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);
            return status;
        }
    }


    PSTORAGE_OBJECT pStorageObject = nullptr;
    if (!g_pStorageObjects)
        return STATUS_DEVICE_NOT_CONNECTED;

    for (auto pcDiskObject : g_pStorageObjects->locked()) {
        if (!pcDiskObject)
            continue;

        bool matchDiskIndex = pcDiskObject->info.diskIndex == pStorageLocation.diskIndex;
        bool matchRawDisk = pcDiskObject->info.isRawDiskObject == pStorageLocation.isRawDiskObject;
        bool matchPartition = true;

        if (pStorageLocation.partitionNumber != (ULONG)-1) {
            matchPartition = pcDiskObject->info.partitionNumber == pStorageLocation.partitionNumber;
        }

        if (matchDiskIndex && matchRawDisk && matchPartition) {
            pStorageObject = pcDiskObject;
            break;
        }
    }

    if (!pStorageObject && (pIrpStack->Parameters.DeviceIoControl.IoControlCode != IOCTL_GET_DISK_INFO)) {
        LOG("Requested disk/partition not found: index=%lu isRaw=%u\n", pStorageLocation.diskIndex, pStorageLocation.isRawDiskObject);
        status = STATUS_DEVICE_NOT_CONNECTED;
        pIrp->IoStatus.Status = status;
        IoCompleteRequest(pIrp, IO_NO_INCREMENT);
        return status;
    }

    status = RefreshGlobalStorageObjects();
    if (!NT_SUCCESS(status)) {
        LOG("Refresh global storage objects failed: 0x%08X\n", status);
        pIrp->IoStatus.Status = status;
        IoCompleteRequest(pIrp, IO_NO_INCREMENT);
        return status;
    }

    switch (pIrpStack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_SECTOR_READ:
        status = ReadSectorIoctlHandler(pIrp, pIrpStack, pStorageObject, &pStorageLocation);
        break;
    case IOCTL_SECTOR_WRITE:
        status = WriteSectorIoctlHandler(pIrp, pIrpStack, pStorageObject, &pStorageLocation);
        break;
    case IOCTL_GET_SECTOR_SIZE:
        status = GetSectorSizeIoctlHandler(pIrp, pStorageObject);
        break;
    case IOCTL_GET_DISK_INFO:
        status = StorageInfoIoctlHandler(pIrp, pIrpStack);
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    pIrp->IoStatus.Status = status;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS DriverDefaultIrpHandler(IN PDEVICE_OBJECT pDeviceObject, IN PIRP pIrp) {
	LOG("DriverDefaultIrpHandler called\n");
	UNREFERENCED_PARAMETER(pDeviceObject);
	pIrp->IoStatus.Information = 0;
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

PDEVICE_OBJECT g_pDeviceObject = NULL;
UNICODE_STRING g_dosDeviceName;

VOID DriverUnload(IN PDRIVER_OBJECT pDriverObject) {
	UNREFERENCED_PARAMETER(pDriverObject);
	LOG("DriverUnload called\n");

	FreeCollectedStorageObjects();

	IoDeleteSymbolicLink(&g_dosDeviceName);
	IoDeleteDevice(g_pDeviceObject);
	return;
}

extern "C" NTSTATUS DriverEntry(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING pRegistryPath) {
	UNREFERENCED_PARAMETER(pRegistryPath);
	UNICODE_STRING deviceName;

	LOG("DriverEntry called\n");
	RtlInitUnicodeString(&deviceName, L"\\Device\\SectorIO");
	RtlInitUnicodeString(&g_dosDeviceName, L"\\DosDevices\\SectorIO");
	

#if _SECURE_DEVICE
    UNICODE_STRING deviceSecurityDescriptor = SDDL_DEVOBJ_SYS_ALL_ADM_ALL;
    NTSTATUS status = WdmlibIoCreateDeviceSecure(pDriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &deviceSecurityDescriptor, NULL, &g_pDeviceObject);

#else
    NTSTATUS status = IoCreateDevice(pDriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &g_pDeviceObject);
#endif

	if (!NT_SUCCESS(status))
		return status;

	status = IoCreateSymbolicLink(&g_dosDeviceName, &deviceName);
	if (!NT_SUCCESS(status)) {
        LOG("IoCreateSymbolicLink failed: 0x%08X", status);
		IoDeleteDevice(g_pDeviceObject);
		return status;
	}

	g_pStorageObjects = new (NonPagedPool) vector<PSTORAGE_OBJECT>();
	if (!g_pStorageObjects) {
        LOG("Global storage objects memory allocation failed\n");
		IoDeleteDevice(g_pDeviceObject);
		IoDeleteSymbolicLink(&g_dosDeviceName);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = RefreshGlobalStorageObjects();
	if (!NT_SUCCESS(status))
	{
        LOG("RefreshGlobalStorageObjects failed: 0x%08X", status);
		IoDeleteDevice(g_pDeviceObject);
		IoDeleteSymbolicLink(&g_dosDeviceName);
		return status;
	}

	for (ULONG i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
		pDriverObject->MajorFunction[i] = DriverDefaultIrpHandler;
	
	pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverIoDeviceDispatchRoutine;
	pDriverObject->DriverUnload = DriverUnload;
	return status;
}
