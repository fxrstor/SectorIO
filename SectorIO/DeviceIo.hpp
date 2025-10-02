#pragma once
#include "Driver.hpp"

void KSleep(ULONG seconds);
NTSTATUS IoDeviceControl(IN PDEVICE_OBJECT pDeviceObject, IN ULONG ioControlCode, IN PVOID inputBuffer OPTIONAL, IN ULONG inputBufferLength, OUT PVOID outputBuffer OPTIONAL, IN ULONG outputBufferLength, OUT PULONG_PTR information OPTIONAL);
