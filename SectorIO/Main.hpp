#pragma once

#define LOG(x, ...) DbgPrint("SectorIO: " x, __VA_ARGS__)

#define DRIVER_TAG 'oIeS'

// requires windows 10 2004 or above
//#define NO_DEPRECATED_FUNCTIONS