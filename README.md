# SectorIO
Kernel mode driver for arbitrary sector read and write on the underlying physical disk.

## Usage
Check the `example.cpp` file.

## TODOs
- [ ] Add support for arbitrary partition sector R/W - I assume there was partition support in the original driver (see the Inspiration section) but it no longer works in latest versions of Windows (from 7 iirc).
AFAIK, I tested with PartMgr.sys, didn't work. I also added the code to check for newer Partition names (not `DP(`), zero results with that too.
- [ ] Add optional support for WdmlibIoCreateDeviceSecure so non-admin processes cannot access the driver. 
- [ ] Code is pretty messy, improve it.

## Inspiration
Special thanks to this broken [repo](https://github.com/jschicht/SectorIo), to fix and improve that driver is the intention of this repository.
