#include "Sector.hpp"

vector<PDISK_OBJECT>* g_pDiskObjects = nullptr;

NTSTATUS GetGeometry(PDEVICE_OBJECT pDiskDeviceObject, PDISK_GEOMETRY pDiskGeometry)
{
	LOG("GetGeometry called\n");
	IO_STATUS_BLOCK ioStatusBlock;
	KEVENT event;

	KeInitializeEvent(&event, NotificationEvent, FALSE);

	PIRP pIrp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_DRIVE_GEOMETRY, pDiskDeviceObject, NULL, 0, pDiskGeometry, sizeof(DISK_GEOMETRY), FALSE, &event, &ioStatusBlock);

	if (!pIrp)
		return STATUS_INSUFFICIENT_RESOURCES;
	NTSTATUS status = IoCallDriver(pDiskDeviceObject, pIrp);
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = ioStatusBlock.Status;
	}

	LOG("GetGeometry status: 0x%X\n", status);
	return status;
}

NTSTATUS GetAllDiskObjects() {
	LOG("GetAllDiskObjects called\n");
	UNICODE_STRING DiskDriverPath;
	PDRIVER_OBJECT pDiskDriverObject;
	RtlInitUnicodeString(&DiskDriverPath, L"\\Driver\\Disk");

	NTSTATUS status = ObReferenceObjectByName(&DiskDriverPath, OBJ_CASE_INSENSITIVE, 0, 0, *IoDriverObjectType, KernelMode, 0, (PVOID*)&pDiskDriverObject);
	if (!NT_SUCCESS(status)) {
		LOG("ObReferenceObjectByName failed: 0x%X\n", status);
		return status;
	}

	PDEVICE_OBJECT pCurrentDeviceObject = pDiskDriverObject->DeviceObject;
	if (!pDiskDriverObject->DeviceObject) {
		LOG("pDiskDriverObject (disk.sys) is empty\n");
		return STATUS_DATA_ERROR;
	}

	PDISK_GEOMETRY pDiskGeometry = new(PAGED_POOL) DISK_GEOMETRY;
	if (!pDiskGeometry) {
		LOG("ExAllocatePool failed to allocate memory in NonPagedPool\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	do {
		RtlZeroMemory(pDiskGeometry, sizeof(DISK_GEOMETRY));
		LOG("DeviceType: %d\n", pCurrentDeviceObject->DeviceType);
		if (pCurrentDeviceObject->DeviceType != FILE_DEVICE_DISK || !(pCurrentDeviceObject->Flags & DO_DEVICE_HAS_NAME)) {
			pCurrentDeviceObject = pCurrentDeviceObject->NextDevice;
			continue;
		}

		ULONG returnLength;
		ObQueryNameString(pCurrentDeviceObject, NULL, 0, &returnLength);
		if (returnLength == 0) {
			pCurrentDeviceObject = pCurrentDeviceObject->NextDevice;
			continue;
		}

		POBJECT_NAME_INFORMATION pNameBuffer = (POBJECT_NAME_INFORMATION)AllocateMemory(PAGED_POOL, returnLength, DRIVER_TAG);
		if (!pNameBuffer)
		{
			LOG("Failed to allocate name buffer\n");
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		if (!NT_SUCCESS(ObQueryNameString(pCurrentDeviceObject, pNameBuffer, returnLength, &returnLength)) || !pNameBuffer->Name.Buffer) {
			ExFreePoolWithTag(pNameBuffer, 0);
			pCurrentDeviceObject = pCurrentDeviceObject->NextDevice;
			continue;
		}
		LOG("Found Disk Device: \"%.*ws\"\n", (unsigned int)(pNameBuffer->Name.Length / sizeof(WCHAR)), pNameBuffer->Name.Buffer);

		ULONG diskIndex = 0;
		PWCHAR fullName = pNameBuffer->Name.Buffer;

		PWCHAR hardDiskPtr = wcsstr(fullName, L"Harddisk");
		if (!hardDiskPtr) {
			ExFreePoolWithTag(pNameBuffer, 0);
			pCurrentDeviceObject = pCurrentDeviceObject->NextDevice;
			continue;
		}

		hardDiskPtr += wcslen(L"Harddisk");
		WCHAR digitBuffer[16] = { 0 };
		int i = 0;
		while (iswdigit(hardDiskPtr[i]) && i < (int)(sizeof(digitBuffer) / sizeof(WCHAR) - 1)) {
			digitBuffer[i] = hardDiskPtr[i];
			i++;
		}
		digitBuffer[i] = L'\0';
		UNICODE_STRING hardDiskIndexString;
		RtlInitUnicodeString(&hardDiskIndexString, digitBuffer);
		RtlUnicodeStringToInteger(&hardDiskIndexString, 0, &diskIndex);

		if (!wcsstr(fullName, L"\\DR")) {
			FreePtr(pNameBuffer);
			pCurrentDeviceObject = pCurrentDeviceObject->NextDevice;
			continue;
		}

		PDISK_OBJECT pDiskObject = new(PAGED_POOL) DISK_OBJECT;
		if (!pDiskObject) {
			LOG("Failed to allocate DISK_OBJ\n");
			FreePtr(pNameBuffer);
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		LOG("Disk object parsed: diskIndex=%lu\n", diskIndex);
		pDiskObject->diskIndex = diskIndex;
		pDiskObject->pDiskDeviceObject = pCurrentDeviceObject;
		pDiskObject->geometryFound = FALSE;
		pDiskObject->sectorSize = 0;

		status = GetGeometry(pDiskObject->pDiskDeviceObject, pDiskGeometry);
		if (NT_SUCCESS(status)) {
			pDiskObject->geometryFound = TRUE;
			pDiskObject->sectorSize = pDiskGeometry->BytesPerSector;
		}
		else
			pDiskObject->geometryFound = FALSE;

		g_pDiskObjects->push_back(pDiskObject);
		FreePtr(pNameBuffer);
		pCurrentDeviceObject = pCurrentDeviceObject->NextDevice;
	} while (pCurrentDeviceObject);
	FreePtr(pDiskGeometry);
	return status;
}


NTSTATUS GetSectorSizeIoctlHandler(IN PIRP pIrp, IN PDISK_OBJECT pDiskObject) {
	LOG("GetSectorSizeIoctlHandler called\n");
	if (pIrp->UserBuffer == NULL)
		return STATUS_INVALID_USER_BUFFER;

	__try {
		ProbeForWrite(pIrp->UserBuffer, sizeof(ULONG), __alignof(ULONG));
		*(ULONG*)pIrp->UserBuffer = pDiskObject->sectorSize;
		pIrp->IoStatus.Information = sizeof(ULONG);
		return STATUS_SUCCESS;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return GetExceptionCode();
	}
}

NTSTATUS RWIrpCompletion(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PVOID Context) {
	UNREFERENCED_PARAMETER(DeviceObject);
	PIOCTL_COMPLETION_CONTEXT ctx = (PIOCTL_COMPLETION_CONTEXT)Context;
	ctx->ioStatusBlock.Status = Irp->IoStatus.Status;
	ctx->ioStatusBlock.Information = Irp->IoStatus.Information;
	KeSetEvent(&ctx->event, IO_NO_INCREMENT, FALSE);
	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS ReadSectorIoctlHandler(IN PIRP pIrp, IN PIO_STACK_LOCATION pIrpStack, IN PDISK_OBJECT pDiskObject, IN PDISK_LOCATION pDiskLocation) {
	LOG("ReadSectorIoctlHandler called\n");

	if ((pIrpStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(DISK_LOCATION)) ||
		((ULONG64)pIrpStack->Parameters.DeviceIoControl.OutputBufferLength < pDiskObject->sectorSize))
		return STATUS_INFO_LENGTH_MISMATCH;

	PMDL mdl = IoAllocateMdl(
		(PUCHAR)pIrp->UserBuffer,
		pIrpStack->Parameters.DeviceIoControl.OutputBufferLength,
		FALSE,
		FALSE,
		NULL
	);
	if (!mdl)
		return STATUS_INSUFFICIENT_RESOURCES;

	__try {
		MmProbeAndLockPages(mdl, UserMode, IoWriteAccess);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		IoFreeMdl(mdl);
		return GetExceptionCode();
	}

	LOG("MDL allocation successful\n");

	__try {
		ProbeForRead(pDiskLocation, sizeof(DISK_LOCATION), __alignof(DISK_LOCATION));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return GetExceptionCode();
	}
	LOG("PDISK_LOCATION probe for read successful\n");

	LARGE_INTEGER diskOffset;
	diskOffset.QuadPart = (LONGLONG)pDiskObject->sectorSize * (LONGLONG)pDiskLocation->sectorNumber;

	PIRP lowerIrp = IoAllocateIrp(pDiskObject->pDiskDeviceObject->StackSize, FALSE);
	if (!lowerIrp) {
		LOG("IoAllocateIrp failed\n");
		MmUnlockPages(mdl);
		IoFreeMdl(mdl);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	IOCTL_COMPLETION_CONTEXT ctx;
	KeInitializeEvent(&ctx.event, NotificationEvent, FALSE);
	IoSetCompletionRoutine(lowerIrp, RWIrpCompletion, &ctx, TRUE, TRUE, TRUE);

	PIO_STACK_LOCATION nextSp = IoGetNextIrpStackLocation(lowerIrp);
	nextSp->MajorFunction = IRP_MJ_READ;
	nextSp->DeviceObject = pDiskObject->pDiskDeviceObject;
	nextSp->Parameters.Read.Length = pIrpStack->Parameters.DeviceIoControl.OutputBufferLength;
	nextSp->Parameters.Read.ByteOffset = diskOffset;
	// In older Windows versions or WDK headers, I think you'd do:
	// nextSp->Parameters.Read.MdlAddress = mdl;

	lowerIrp->MdlAddress = mdl;
	ULONG bytesWritten;
	NTSTATUS status = IoCallDriver(pDiskObject->pDiskDeviceObject, lowerIrp);
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&ctx.event, Executive, KernelMode, FALSE, NULL);
		status = ctx.ioStatusBlock.Status;
		bytesWritten = (ULONG)ctx.ioStatusBlock.Information;
	}
	else
		bytesWritten = (ULONG)ctx.ioStatusBlock.Information;

	MmUnlockPages(mdl);
	IoFreeMdl(mdl);
	IoFreeIrp(lowerIrp);

	if (NT_SUCCESS(status))
		pIrp->IoStatus.Information = bytesWritten;

	LOG("IoCallDriver (READ) returned 0x%08X\n", status);
	return status;
}

NTSTATUS WriteSectorIoctlHandler(IN PIRP pIrp, IN PIO_STACK_LOCATION pIrpStack, IN PDISK_OBJECT pDiskObject, IN PDISK_LOCATION pDiskLocation) {
	LOG("WriteSectorIoctlHandler called\n");

	if ((pIrpStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(DISK_LOCATION)) ||
		(ULONG)pIrpStack->Parameters.DeviceIoControl.OutputBufferLength < pDiskObject->sectorSize)
		return STATUS_INFO_LENGTH_MISMATCH;

	PMDL mdl = IoAllocateMdl(
		(PUCHAR)pIrp->UserBuffer,
		pIrpStack->Parameters.DeviceIoControl.OutputBufferLength,
		FALSE,
		FALSE,
		NULL
	);
	if (!mdl)
		return STATUS_INSUFFICIENT_RESOURCES;

	__try {
		MmProbeAndLockPages(mdl, UserMode, IoReadAccess);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		IoFreeMdl(mdl);
		return GetExceptionCode();
	}
	LOG("MDL allocation successful\n");
	__try {
		ProbeForRead(pDiskLocation, sizeof(DISK_LOCATION), __alignof(DISK_LOCATION));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return GetExceptionCode();
	}
	LOG("PDISK_LOCATION probe for read successful\n");

	LARGE_INTEGER diskOffset;
	diskOffset.QuadPart = (LONGLONG)pDiskObject->sectorSize * (LONGLONG)pDiskLocation->sectorNumber;

	PIRP lowerIrp = IoAllocateIrp(pDiskObject->pDiskDeviceObject->StackSize, FALSE);
	if (!lowerIrp) {
		MmUnlockPages(mdl);
		IoFreeMdl(mdl);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	IOCTL_COMPLETION_CONTEXT ctx;
	KeInitializeEvent(&ctx.event, NotificationEvent, FALSE);

	IoSetCompletionRoutine(lowerIrp, RWIrpCompletion, &ctx, TRUE, TRUE, TRUE);
	{
		PIO_STACK_LOCATION nextSp = IoGetNextIrpStackLocation(lowerIrp);
		nextSp->MajorFunction = IRP_MJ_WRITE;
		nextSp->DeviceObject = pDiskObject->pDiskDeviceObject;
		nextSp->Parameters.Write.Length = pIrpStack->Parameters.DeviceIoControl.OutputBufferLength;
		nextSp->Parameters.Write.ByteOffset = diskOffset;
		nextSp->Flags |= SL_FORCE_DIRECT_WRITE | SL_OVERRIDE_VERIFY_VOLUME;
	}
	lowerIrp->MdlAddress = mdl;

	NTSTATUS status = IoCallDriver(pDiskObject->pDiskDeviceObject, lowerIrp);
	ULONG bytesWritten;
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&ctx.event, Executive, KernelMode, FALSE, NULL);
		status = ctx.ioStatusBlock.Status;
		bytesWritten = (ULONG)ctx.ioStatusBlock.Information;
	}
	else
		bytesWritten = (ULONG)ctx.ioStatusBlock.Information;

	MmUnlockPages(mdl);
	IoFreeMdl(mdl);
	IoFreeIrp(lowerIrp);

	if (NT_SUCCESS(status))
		pIrp->IoStatus.Information = bytesWritten;

	LOG("IoCallDriver (WRITE) returned 0x%08X\n", status);
	return status;
}
