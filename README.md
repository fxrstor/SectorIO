# SectorIO
Kernel mode driver for arbitrary sector read and write on the underlying physical disk.

## Usage
Check the `example.cpp` file.

## Why VS2019 when VS2022 is available?
There is a reason I chose VS2019 instead of VS2022 as the IDE because I wanted to use WDK version 19045 (2004) along with Windows SDK 19045 (2004) and they are only available in VS2019. 
I chose WDK 19045 (2004) because for some reason Microsoft's latest WDK (as of writing this README) has broken the compatibility with the version of Windows 10 I was using, henceforth while loading the driver, it was producing dependency errors (`The specified procedure could not be found.`).
WDK 19045 ensures compatibility from Windows 7 to 11 (tested successfully).

## TODOs
- [ ] Add support for arbitrary partition sector R/W - I assume there was partition support in the original driver (see the Inspiration section) but it no longer works in latest versions of Windows (from 7 iirc).
AFAIK, I tested with PartMgr.sys, didn't work. I also added the code to check for newer Partition names (not `DP(`), zero results with that too.
- [ ] Add optional support for WdmlibIoCreateDeviceSecure so non-admin processes cannot access the driver. 
- [ ] Code is pretty messy, improve it.

## Inspiration
Special thanks to this broken [repo](https://github.com/jschicht/SectorIo), to fix and improve that driver is the intention of this repository.
