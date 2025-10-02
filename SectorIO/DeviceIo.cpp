#include "DeviceIo.hpp"

void KSleep(ULONG seconds) {
	LARGE_INTEGER time;
	time.QuadPart = -(LONGLONG)seconds * 10000000;
	KeDelayExecutionThread(KernelMode, FALSE, &time);
}

NTSTATUS IoDeviceControl(IN PDEVICE_OBJECT pDeviceObject, IN ULONG ioControlCode, IN PVOID inputBuffer OPTIONAL, IN ULONG inputBufferLength, OUT PVOID outputBuffer OPTIONAL, IN ULONG outputBufferLength, OUT PULONG_PTR information OPTIONAL) {
	IO_STATUS_BLOCK ioStatusBlock;
	KEVENT completionEvent;
	KeInitializeEvent(&completionEvent, NotificationEvent, FALSE);

	PIRP pIrp = IoBuildDeviceIoControlRequest(ioControlCode, pDeviceObject, inputBuffer, inputBufferLength, outputBuffer, outputBufferLength, FALSE, &completionEvent, &ioStatusBlock);
	if (!pIrp)
		return STATUS_INSUFFICIENT_RESOURCES;

	NTSTATUS status = IoCallDriver(pDeviceObject, pIrp);
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&completionEvent, Executive, KernelMode, FALSE, NULL);
		status = ioStatusBlock.Status;
	}
	if (information) *information = (ULONG_PTR)ioStatusBlock.Information;
	return status;
}
