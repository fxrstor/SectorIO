# SectorIO
Kernel mode driver for arbitrary sector read and write on the underlying physical disk.

## Usage
Check the `example.cpp` file.

## Why VS2019 when VS2022 is available?
There is a reason I chose VS2019 instead of VS2022 as the IDE because I wanted to use WDK version 19045 (2004) along with Windows SDK 19045 (2004) and they are only available in VS2019. 
I chose WDK 19045 (2004) because for some reason Microsoft's latest WDK (as of writing this README) has broken the compatibility with the version of Windows 10 I was using, henceforth while loading the driver, it was producing dependency errors (`The specified procedure could not be found.`).
WDK 19045 ensures compatibility from Windows 7 to 11 (tested successfully).

## TODOs
- [X] Add support for arbitrary partition sector R/W - I assume there was partition support in the original driver (see the Inspiration section) but it no longer works in latest versions of Windows (from 7 iirc).
AFAIK, I tested with PartMgr.sys, didn't work. I also added the code to check for newer Partition names (not `DP(`), zero results with that too. Possible solution: look into `DiskCryptor` program and see how it performs operations to disks and partitions. (see PR #1)
- [X] Remove the dependency on undocumented API as they are subjected to private changes (although unlikely).
- [X] Add optional support for WdmlibIoCreateDeviceSecure so non-admin processes cannot access the driver. 
- [X] Code is pretty messy, improve it. (kind of)
- [ ] Ensure consistency across debug messages.
 
## Inspiration
Special thanks to this broken [repo](https://github.com/jschicht/SectorIo). To fix and improve that driver is the aim of this repository.
