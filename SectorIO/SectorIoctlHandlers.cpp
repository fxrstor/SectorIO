#include "SectorIoctlHandlers.hpp"

NTSTATUS RWIrpCompletion(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PVOID Context) {
	UNREFERENCED_PARAMETER(DeviceObject);
	PIOCTL_COMPLETION_CONTEXT ctx = (PIOCTL_COMPLETION_CONTEXT)Context;
	ctx->ioStatusBlock.Status = Irp->IoStatus.Status;
	ctx->ioStatusBlock.Information = Irp->IoStatus.Information;
	KeSetEvent(&ctx->event, IO_NO_INCREMENT, FALSE);
	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS GetSectorSizeIoctlHandler(IN PIRP pIrp, IN PSTORAGE_OBJECT pStorageObject) {
	LOG("GetSectorSizeIoctlHandler called\n");
	if (pIrp->UserBuffer == NULL || !pStorageObject)
		return STATUS_INVALID_PARAMETER;

	__try {
		ProbeForWrite(pIrp->UserBuffer, sizeof(ULONG), __alignof(ULONG));
		*(ULONG*)pIrp->UserBuffer = pStorageObject->info.sectorSize;
		pIrp->IoStatus.Information = sizeof(ULONG);
		return STATUS_SUCCESS;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return GetExceptionCode();
	}
}

NTSTATUS PerformSectorIoOperation(IN PIRP pIrp, IN PIO_STACK_LOCATION pIrpStack, IN PSTORAGE_OBJECT pStorageObject, IN PSTORAGE_LOCATION pStorageLocation, IN BOOLEAN isWrite)
{
	NTSTATUS status = STATUS_SUCCESS;
    PIRP lowerIrp = NULL;
	IOCTL_COMPLETION_CONTEXT ctx;

	if (!pStorageObject)
		return STATUS_INVALID_DEVICE_REQUEST;

	if ((pIrpStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(STORAGE_LOCATION)) ||
		((ULONG64)pIrpStack->Parameters.DeviceIoControl.OutputBufferLength < pStorageObject->info.sectorSize))
		return STATUS_INFO_LENGTH_MISMATCH;

	LARGE_INTEGER startingOffset;
	startingOffset.QuadPart = (LONGLONG)pStorageObject->info.partitionStartingOffset;

	LOG("  Attempting to allocate an MDL\n");
	PMDL mdl = IoAllocateMdl(
		(PVOID)pIrp->UserBuffer,
		pIrpStack->Parameters.DeviceIoControl.OutputBufferLength,
		FALSE,
		FALSE,
		NULL
	);
	if (!mdl) {
        LOG("  IoAllocateMdl failed\n");
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto Done;
	}

	__try {
		MmProbeAndLockPages(mdl, UserMode, isWrite ? IoReadAccess : IoWriteAccess);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		status = GetExceptionCode();
        LOG("  MmProbeAndLockPages exception 0x%08X\n", status);
		goto Done;
	}

	LARGE_INTEGER diskOffset;
	diskOffset.QuadPart = (LONGLONG)pStorageObject->info.sectorSize * (LONGLONG)pStorageLocation->sectorNumber;
        
    lowerIrp = IoAllocateIrp(pStorageObject->pStorageDeviceObject->StackSize, FALSE);
	if (!lowerIrp) {
        LOG("  IoAllocateIrp failed\n");
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto Done;
	}

	KeInitializeEvent(&ctx.event, NotificationEvent, FALSE);
	IoSetCompletionRoutine(lowerIrp, RWIrpCompletion, &ctx, TRUE, TRUE, TRUE);

	PIO_STACK_LOCATION nextSp = IoGetNextIrpStackLocation(lowerIrp);
	if (isWrite) {
		nextSp->MajorFunction = IRP_MJ_WRITE;
		nextSp->Parameters.Write.Length = pIrpStack->Parameters.DeviceIoControl.OutputBufferLength;
		nextSp->Parameters.Write.ByteOffset = diskOffset;
		nextSp->Flags |= SL_FORCE_DIRECT_WRITE | SL_OVERRIDE_VERIFY_VOLUME;
	}
	else {
		nextSp->MajorFunction = IRP_MJ_READ;
		nextSp->Parameters.Read.Length = pIrpStack->Parameters.DeviceIoControl.OutputBufferLength;
		nextSp->Parameters.Read.ByteOffset = diskOffset;
	}
	nextSp->DeviceObject = pStorageObject->pStorageDeviceObject;
	lowerIrp->MdlAddress = mdl;

    LOG("  Sending lower IRP %s: device=%p offset=%llu length=%u\n",
        isWrite ? "WRITE" : "READ",
        pStorageObject->pStorageDeviceObject,
        (unsigned long long)diskOffset.QuadPart,
        pIrpStack->Parameters.DeviceIoControl.OutputBufferLength);

	status = IoCallDriver(pStorageObject->pStorageDeviceObject, lowerIrp);
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&ctx.event, Executive, KernelMode, FALSE, NULL);
		status = ctx.ioStatusBlock.Status;
	}

	if (NT_SUCCESS(status))
		pIrp->IoStatus.Information = (ULONG)ctx.ioStatusBlock.Information;
    else
        LOG("  IoCallDriver failed with status 0x%08X\n", status);

Done:
	if (mdl) {
		__try {
			MmUnlockPages(mdl);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { }
		IoFreeMdl(mdl);
	}
	if (lowerIrp)
		IoFreeIrp(lowerIrp);

    LOG("PerformSectorIoOperation complete, status=0x%08X\n", status);
	return status;
}

NTSTATUS ReadSectorIoctlHandler(IN PIRP pIrp, IN PIO_STACK_LOCATION pIrpStack, IN PSTORAGE_OBJECT pStorageObject, IN PSTORAGE_LOCATION pStorageLocation) {
	LOG("ReadSectorIoctlHandler -> PerformSectorIoOperation (isWrite = FALSE)\n");
	return PerformSectorIoOperation(pIrp, pIrpStack, pStorageObject, pStorageLocation, FALSE);
}

NTSTATUS WriteSectorIoctlHandler(IN PIRP pIrp, IN PIO_STACK_LOCATION pIrpStack, IN PSTORAGE_OBJECT pDiskObject, IN PSTORAGE_LOCATION pDiskLocation) {
	LOG("WriteSectorIoctlHandler -> PerformSectorIoOperation (isWrite = TRUE)\n");
	return PerformSectorIoOperation(pIrp, pIrpStack, pDiskObject, pDiskLocation, TRUE);
}

static NTSTATUS CopySingleStorageObjectInfoToUser(IN PIRP pIrp, IN PSTORAGE_LOCATION sel, IN PVOID outBuffer, IN ULONG outLength) {
    if (!sel || !outBuffer)
        return STATUS_INVALID_PARAMETER;

    __try {
        ProbeForRead(sel, sizeof(STORAGE_LOCATION), __alignof(STORAGE_LOCATION));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }

    PSTORAGE_OBJECT found = nullptr;
    LOG("CopySingleStorageObjectInfoToUser: searching diskIndex=%u partition=%u isRaw=%u\n", sel->diskIndex, sel->partitionNumber, sel->isRawDiskObject);

    for (auto entry : g_pStorageObjects->locked()) {
        if (!entry) continue;
        if (entry->info.diskIndex == sel->diskIndex &&
            entry->info.isRawDiskObject == sel->isRawDiskObject &&
            (sel->partitionNumber == (ULONG)-1 || entry->info.partitionNumber == sel->partitionNumber)) {
            found = entry;
            break;
        }
    }

    if (!found) {
        LOG("  no matching storage object found\n");
        return STATUS_NOT_FOUND;
    }

    if (outLength < sizeof(STORAGE_OBJECT_INFO)) {
        LOG("  outLength too small (%u < %u)\n", outLength, sizeof(STORAGE_OBJECT_INFO));
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    __try {
        RtlCopyMemory(outBuffer, &found->info, sizeof(STORAGE_OBJECT_INFO));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        NTSTATUS ex = GetExceptionCode();
        LOG("  exception copying STORAGE_OBJECT_INFO -> 0x%08X\n", ex);
        return ex;
    }

    pIrp->IoStatus.Information = sizeof(STORAGE_OBJECT_INFO);
    LOG("  copied STORAGE_OBJECT_INFO to %p\n", outBuffer);
    return STATUS_SUCCESS;
}

NTSTATUS StorageInfoIoctlHandler(IN PIRP pIrp, IN PIO_STACK_LOCATION pIrpStack) {
    LOG("StorageInfoIoctlHandler called\n");
    if (!g_pStorageObjects) {
        LOG("  g_pStorageObjects is NULL -> STATUS_UNSUCCESSFUL\n");
        return STATUS_UNSUCCESSFUL;
    }

    ULONG outLength = pIrpStack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID outBuffer = pIrp->UserBuffer;
    LOG("  outBuffer=%p outLength=%u\n", outBuffer, outLength);

    if (outBuffer == NULL || outLength == 0) {
        LOG("  Invalid parameter: outBuffer==NULL or outLength==0 -> STATUS_INVALID_PARAMETER\n");
        return STATUS_INVALID_PARAMETER;
    }

    __try {
        LOG("  ProbeForWrite(outBuffer=%p, outLength=%u)\n", outBuffer, outLength);
        ProbeForWrite(outBuffer, outLength, __alignof(ULONG));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        NTSTATUS ex = GetExceptionCode();
        LOG("  Exception in ProbeForWrite -> 0x%08X\n", ex);
        return ex;
    }

    if (pIrpStack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(STORAGE_LOCATION)) {
        PSTORAGE_LOCATION sel = (PSTORAGE_LOCATION)pIrpStack->Parameters.DeviceIoControl.Type3InputBuffer;
        LOG("  InputBufferLength indicates STORAGE_LOCATION present: sel=%p\n", sel);

        NTSTATUS status = CopySingleStorageObjectInfoToUser(pIrp, sel, outBuffer, outLength);
        if (!NT_SUCCESS(status)) {
            LOG("  CopySingleStorageObjectInfoToUser failed -> 0x%08X\n", status);
        }
        else {
            LOG("  Copied single STORAGE_OBJECT_INFO successfully\n");
        }
        return status;
    }

    vector<STORAGE_OBJECT_INFO> snapshot;
    for (auto entry : g_pStorageObjects->locked()) {
        if (entry) snapshot.push_back(entry->info);
    }

    SIZE_T requiredBytes = snapshot.size() * sizeof(STORAGE_OBJECT_INFO);
    LOG("  snapshot count=%llu requiredBytes=%llu\n", (unsigned long long)snapshot.size(), (unsigned long long)requiredBytes);

    if (requiredBytes == 0) {
        __try {
            if (outLength >= sizeof(SIZE_T)) {
                *(SIZE_T*)outBuffer = 0;
                pIrp->IoStatus.Information = sizeof(SIZE_T);
                LOG("  no storage objects: wrote required size 0\n");
                return STATUS_SUCCESS;
            }
            else {
                LOG("  outLength (%u) < sizeof(SIZE_T) -> STATUS_INFO_LENGTH_MISMATCH\n", outLength);
                return STATUS_INFO_LENGTH_MISMATCH;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            NTSTATUS ex = GetExceptionCode();
            LOG("  exception writing zero size -> 0x%08X\n", ex);
            return ex;
        }
    }

    if ((SIZE_T)outLength >= requiredBytes) {
        __try {
            RtlCopyMemory(outBuffer, snapshot.data(), requiredBytes);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            NTSTATUS ex = GetExceptionCode();
            LOG("    exception copying snapshot -> 0x%08X\n", ex);
            pIrp->IoStatus.Information = 0;
            return ex;
        }

        pIrp->IoStatus.Information = (ULONG)requiredBytes;
        LOG("  copied %llu bytes of STORAGE_OBJECT_INFO to %p\n", (unsigned long long)requiredBytes, outBuffer);
        return STATUS_SUCCESS;
    }

    if ((SIZE_T)outLength >= sizeof(SIZE_T)) {
        __try {
            *(SIZE_T*)outBuffer = requiredBytes;
            pIrp->IoStatus.Information = sizeof(SIZE_T);
            LOG("  outBuffer too small: wrote requiredBytes=%llu into outBuffer\n", (unsigned long long)requiredBytes);
            return STATUS_BUFFER_TOO_SMALL;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            NTSTATUS ex = GetExceptionCode();
            LOG("  exception writing required size -> 0x%08X\n", ex);
            pIrp->IoStatus.Information = 0;
            return ex;
        }
    }

    LOG("  outLength (%u) < sizeof(SIZE_T) -> STATUS_INFO_LENGTH_MISMATCH\n", outLength);
    return STATUS_INFO_LENGTH_MISMATCH;

}
