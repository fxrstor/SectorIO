#include "Sector.hpp"

vector<PSTORAGE_OBJECT>* g_pStorageObjects = nullptr;

void FreeCollectedStorageObjects() {
    if (!g_pStorageObjects) return;
    for (auto pDiskObject : g_pStorageObjects->locked()) {
        if (!pDiskObject) continue;
        if (pDiskObject->pStorageDeviceObject) ObDereferenceObject(pDiskObject->pStorageDeviceObject);
        delete pDiskObject;
    }

    delete g_pStorageObjects;
    g_pStorageObjects = nullptr;
}

static BOOLEAN IsDeviceObjectInList(IN PDEVICE_OBJECT inpDeviceObject) {
	if (!g_pStorageObjects) return FALSE;
	for (auto pDiskObject : g_pStorageObjects->locked()) {
		if (pDiskObject && pDiskObject->pStorageDeviceObject == inpDeviceObject)
			return TRUE;
	}
	return FALSE;
}

static NTSTATUS AddStorageObject(IN PDEVICE_OBJECT pdo) {
    PSTORAGE_OBJECT pStorageObject = new (NON_PAGED) STORAGE_OBJECT;
    if (!pStorageObject) 
        return STATUS_INSUFFICIENT_RESOURCES;
   
    RtlZeroMemory(pStorageObject, sizeof(*pStorageObject));
    pStorageObject->pStorageDeviceObject = pdo;
    pStorageObject->info.isRawDiskObject = TRUE;
    pStorageObject->info.diskIndex = (ULONG)-1;
    pStorageObject->info.partitionNumber = (ULONG)-1;
    pStorageObject->info.partitionStartingOffset = 0;
    pStorageObject->info.partitionSizeBytes = 0;
    pStorageObject->info.diskSizeBytes = 0;
    pStorageObject->info.sectorSize = 0;

    ObReferenceObject(pdo);

    STORAGE_DEVICE_NUMBER sdn;
    RtlZeroMemory(&sdn, sizeof(sdn));
    NTSTATUS status = IoDeviceControl(pdo, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), NULL);
    if (!NT_SUCCESS(status)) {
        DbgPrint("IOCTL_STORAGE_GET_DEVICE_NUMBER failed: Status=%08x\n", status);
        goto cleanup;
    }

    pStorageObject->info.diskIndex = sdn.DeviceNumber;
    pStorageObject->info.partitionNumber = sdn.PartitionNumber;
    pStorageObject->info.isRawDiskObject = sdn.PartitionNumber == PARTITION_ENTRY_UNUSED || sdn.PartitionNumber == 0;
    DbgPrint("Parsed DiskIndex=%u, Partition=%u\n",  pStorageObject->info.diskIndex, pStorageObject->info.partitionNumber);

    if (pStorageObject->info.partitionNumber != PARTITION_ENTRY_UNUSED &&
        pStorageObject->info.partitionNumber != (ULONG)-1 &&
        pStorageObject->info.partitionNumber != 0) {
        PARTITION_INFORMATION_EX partitionInfoEx;
        RtlZeroMemory(&partitionInfoEx, sizeof(partitionInfoEx));
        status = IoDeviceControl(pdo, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &partitionInfoEx, sizeof(partitionInfoEx), NULL);
        if (!NT_SUCCESS(status)) {
            DbgPrint("IOCTL_DISK_GET_PARTITION_INFO_EX failed: Status=%08x\n", status);
            goto cleanup;
        }
        pStorageObject->info.partitionStartingOffset = (ULONGLONG)partitionInfoEx.StartingOffset.QuadPart;
        pStorageObject->info.partitionSizeBytes = (ULONGLONG)partitionInfoEx.PartitionLength.QuadPart;
        pStorageObject->info.partitionStyle = partitionInfoEx.PartitionStyle;

        if (partitionInfoEx.PartitionStyle == PARTITION_STYLE_GPT) {
            pStorageObject->info.gptPartitionTypeGuid = partitionInfoEx.Gpt.PartitionType;
            pStorageObject->info.gptPartitionIdGuid = partitionInfoEx.Gpt.PartitionId;
            pStorageObject->info.gptAttributes = partitionInfoEx.Gpt.Attributes;
            RtlCopyMemory(pStorageObject->info.gptName, partitionInfoEx.Gpt.Name, sizeof(pStorageObject->info.gptName));
        }
        else if (partitionInfoEx.PartitionStyle == PARTITION_STYLE_MBR)
            pStorageObject->info.mbrPartitionType = partitionInfoEx.Mbr.PartitionType;
        pStorageObject->info.isRawDiskObject = FALSE;
        DbgPrint("Parsed partition: Start=%llu, Len=%llu, Style=%u\n", pStorageObject->info.partitionStartingOffset, pStorageObject->info.partitionSizeBytes, pStorageObject->info.partitionStyle);
    }

    DISK_GEOMETRY_EX diskGeometryEx;
    RtlZeroMemory(&diskGeometryEx, sizeof(diskGeometryEx));
    status = IoDeviceControl(pdo, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &diskGeometryEx, sizeof(diskGeometryEx), NULL);
    if (status == STATUS_NO_MEDIA_IN_DEVICE) {
        DbgPrint("Skipping no-media device...\n");
        status = STATUS_SUCCESS;
        goto cleanup;
    }
    if (!NT_SUCCESS(status)) {
        DbgPrint("IOCTL_DISK_GET_DRIVE_GEOMETRY_EX failed: Status=%08x\n", status);
        goto cleanup;
    }
    
    pStorageObject->info.sectorSize = (ULONG)diskGeometryEx.Geometry.BytesPerSector;
    DbgPrint("SectorSize=%u\n", pStorageObject->info.sectorSize);


    {
        GET_LENGTH_INFORMATION lengthInfo;
        RtlZeroMemory(&lengthInfo, sizeof(lengthInfo));
        status = IoDeviceControl(pdo, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lengthInfo, sizeof(lengthInfo), NULL);
        if (!NT_SUCCESS(status)) {
            DbgPrint("IOCTL_DISK_GET_LENGTH_INFO failed: Status=%08x\n", status);
            goto cleanup;
        }
        
        pStorageObject->info.diskSizeBytes = (ULONGLONG)lengthInfo.Length.QuadPart;
        DbgPrint("DiskSize=%llu\n", pStorageObject->info.diskSizeBytes);
    }

    if (pStorageObject->info.isRawDiskObject) {
        g_pStorageObjects->push_back((PSTORAGE_OBJECT)pStorageObject);
        return STATUS_SUCCESS;
    }

    {
        ULONG layoutBufferSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 128 * sizeof(PARTITION_INFORMATION_EX);
        PDRIVE_LAYOUT_INFORMATION_EX pLayout = (PDRIVE_LAYOUT_INFORMATION_EX)new (PAGED_POOL) char[layoutBufferSize];
        if (!pLayout) {
            DbgPrint("Failed to allocate drive-layout buffer\n");
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto cleanup;
        }

        RtlZeroMemory(pLayout, layoutBufferSize);
        status = IoDeviceControl(pdo, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, pLayout, layoutBufferSize, NULL);
        if (!NT_SUCCESS(status)) {
            DbgPrint("IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed: Status=%08x\n", status);
            delete pLayout;
            goto cleanup;
        }
        pStorageObject->info.gptDiskId = pLayout->Gpt.DiskId;
        delete pLayout;
    }

    g_pStorageObjects->push_back((PSTORAGE_OBJECT)pStorageObject);
    return STATUS_SUCCESS;

cleanup:
    if (pStorageObject->pStorageDeviceObject) ObDereferenceObject(pStorageObject->pStorageDeviceObject);
    delete pStorageObject;
    return status;
}

NTSTATUS RefreshGlobalStorageObjects() {
	LOG("RefreshGlobalStorageObjects called\n");
	const GUID* interfaces[] = {
		&GUID_DEVINTERFACE_DISK,
		&GUID_DEVINTERFACE_PARTITION,
		&GUID_DEVINTERFACE_VOLUME,
		&GUID_DEVINTERFACE_CDROM
	};

	for (size_t gi = 0; gi < ARRAYSIZE(interfaces); ++gi) {
		PWCHAR symbolicLinkList = NULL;
		NTSTATUS status = IoGetDeviceInterfaces(interfaces[gi], NULL, 0, &symbolicLinkList);
		if (!NT_SUCCESS(status)) {
			LOG("IoGetDeviceInterfaces(%zu) failed: 0x%08X\n", gi, status);
			continue;
		}
		if (!symbolicLinkList) {
			LOG("IoGetDeviceInterfaces(%zu) returned NULL list\n", gi);
			continue;
		}

		LOG("Enumerating interface %zu list at %p\n", gi, symbolicLinkList);

		PWCHAR ptr = symbolicLinkList;
		while (ptr && *ptr) {
			UNICODE_STRING symbolicLink;
			RtlInitUnicodeString(&symbolicLink, ptr);
			LOG("Symbolic link: %wZ\n", &symbolicLink);

			PFILE_OBJECT fileObject = NULL;
			PDEVICE_OBJECT deviceObject = NULL;

			status = IoGetDeviceObjectPointer(&symbolicLink, FILE_READ_ATTRIBUTES, &fileObject, &deviceObject);
			if (!NT_SUCCESS(status)) {
				LOG("IoGetDeviceObjectPointer failed for %wZ: 0x%08X\n", &symbolicLink, status);
				goto nextEntry;
			}
			if (!deviceObject) {
				LOG("IoGetDeviceObjectPointer returned no device object for %wZ\n", &symbolicLink);
				if (fileObject) ObDereferenceObject(fileObject);
				goto nextEntry;
			}

    		BOOLEAN isRaw = FALSE;
			if (interfaces[gi] == &GUID_DEVINTERFACE_DISK || interfaces[gi] == &GUID_DEVINTERFACE_CDROM)
				isRaw = TRUE;
			else
				isRaw = FALSE;

			if (IsDeviceObjectInList(deviceObject)) {
				ObDereferenceObject(deviceObject);
				if (fileObject) ObDereferenceObject(fileObject);
				goto nextEntry;
			}

			if (!NT_SUCCESS(AddStorageObject(deviceObject))) {
				LOG("AddStorageObject failed\n");
				ObDereferenceObject(deviceObject);
				if (fileObject) ObDereferenceObject(fileObject);
				goto nextEntry;
			}

			if (fileObject) ObDereferenceObject(fileObject);

		nextEntry:
			ptr += wcslen(ptr) + 1;
		}

		if (symbolicLinkList) {
			ExFreePool(symbolicLinkList);
            symbolicLinkList = NULL;
		}
	}

	return STATUS_SUCCESS;
}

