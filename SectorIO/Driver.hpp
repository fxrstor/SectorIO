#pragma once
#include <ntifs.h>
#include <ntdddisk.h>

#define LOG(x, ...) DbgPrint("SectorIO: " x, __VA_ARGS__)

#define DRIVER_TAG 'oIeS'


// requires windows 10 2004 or above
//#define NO_DEPRECATED_FUNCTIONS

// Uncomment below line to only allow applications with administrative privileges to communicate with driver
// #define _SECURE_DEVICE

#ifdef _SECURE_DEVICE
#pragma comment(lib, "WdmSec.lib")
#include <WdmSec.h>
#endif
