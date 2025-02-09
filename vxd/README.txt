To build this VxD, I used the following toolchain, which I can't redistribute:

* ML.EXE (Microsoft Macro Assembler Version 6.11d)
* LINK.EXE (Microsoft Incremental Linker Version 5.12.8181)
* MSPDB50.DLL (Needed by the linker; version 6.00.7156)

All three of these binaries came from the Windows 98 DDK, which is available
on archive.org as a RAR file. The binaries are in BINS_DDK.CAB.

The following header files are also required, also from the Windows 98 DDK:

* VMM.INC (timestamp August 3rd, 1998)
* MINIVDD.INC (timestamp August 3rd, 1998)
