#include "Main.hpp"
#include "Sector.hpp"

#define SECTOR_IO_CTL_CODE(id) CTL_CODE(FILE_DEVICE_UNKNOWN, id, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_SECTOR_READ		SECTOR_IO_CTL_CODE(0x800)
#define IOCTL_SECTOR_WRITE		SECTOR_IO_CTL_CODE(0x801)
#define IOCTL_GET_SECTOR_SIZE	SECTOR_IO_CTL_CODE(0x802)

NTSTATUS DriverIoDeviceDispatchRoutine(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	UNREFERENCED_PARAMETER(pDeviceObject);
	NTSTATUS status = STATUS_SUCCESS;
	IoSetCancelRoutine(pIrp, NULL);
	PIO_STACK_LOCATION pIrpStack = IoGetCurrentIrpStackLocation(pIrp);

	LOG("DriverIoDeviceDispatchRoutine called\n");

	PDISK_LOCATION pDiskLocation = (PDISK_LOCATION)pIrpStack->Parameters.DeviceIoControl.Type3InputBuffer;

	PDISK_OBJECT pDiskObject = nullptr;
	for (auto pcDiskObject : *g_pDiskObjects) {
		if (pcDiskObject->diskIndex == pDiskLocation->diskIndex && pcDiskObject->geometryFound) {
			pDiskObject = pcDiskObject;
			break;
		}
	}
	if (!pDiskObject) {
		status = STATUS_DEVICE_NOT_CONNECTED;
		pIrp->IoStatus.Status = status;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return status;
	}

	switch (pIrpStack->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_SECTOR_READ:
		status = ReadSectorIoctlHandler(pIrp, pIrpStack, pDiskObject, pDiskLocation);
		break;
	case IOCTL_SECTOR_WRITE:
		status = WriteSectorIoctlHandler(pIrp, pIrpStack, pDiskObject, pDiskLocation);
		break;
	case IOCTL_GET_SECTOR_SIZE:
		status = GetSectorSizeIoctlHandler(pIrp, pDiskObject);
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
	LOG("DriverUnload\n");

	for (auto pDiskObject : *g_pDiskObjects) {
		FreePtr(pDiskObject);
	}

	g_pDiskObjects->clear();
	delete g_pDiskObjects;

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
	
	NTSTATUS status = IoCreateDevice(pDriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &g_pDeviceObject);
	if (!NT_SUCCESS(status))
		return status;
	status = IoCreateSymbolicLink(&g_dosDeviceName, &deviceName);
	if (!NT_SUCCESS(status)) {
		IoDeleteDevice(g_pDeviceObject);
		return status;
	}

	g_pDiskObjects = new (NonPagedPool) vector<PDISK_OBJECT>();
	if (!g_pDiskObjects) {
		IoDeleteDevice(g_pDeviceObject);
		IoDeleteSymbolicLink(&g_dosDeviceName);
		return status;
	}

	status = GetAllDiskObjects();
	if (!NT_SUCCESS(status))
	{
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
