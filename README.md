# Putty++

#### Current features:
- Full knowledge of Putty 0.80
- Multiple terminal sessions in single frame window.
  No background Putty processes, no window embedding.
  Everything is handled by single process.
- Sign incoming data for not visible terminal sessions.
- Prevent accidental paste of big clipboard data.
- Embedded ConPTY backend from pterm.
- Embedded psftp as SFTP backend (Beta, supports only filenames containing only ASCII characters).
- Find in terminal buffer (Beta, supports case insensitivity only for ASCII characters).

#### Build:

This readme describes steps with MinGW-w64 in Cygwin environment.
Based on this and the original cmake files you can adapt for other environments.

1. Install the cmake, make, git and mingw64-<your arch>-gcc-core Cygwin packages.
2. Download Putty 0.80 source from https://the.earth.li/~sgtatham/putty/0.80/putty-0.80.tar.gz or
   through the release page https://www.chiark.greenend.org.uk/~sgtatham/putty/releases/0.80.html
3. Extract it into same folder like this readme file.
   A new folder named putty-0.80 should appear with the original Putty 0.80 source inside.
4. Remove the following files from the putty-0.80 folder:
   - putty-0.80/be_list.c
   - putty-0.80/windows/conpty.c
   - putty-0.80/windows/dialog.c
   - putty-0.80/windows/platform.h
   - putty-0.80/windows/putty.c
   - putty-0.80/windows/putty.rc
   - putty-0.80/windows/utils/shinydialogbox.c
   - putty-0.80/windows/version.rc2
   - putty-0.80/windows/win-gui-seat.h
   - putty-0.80/windows/window.c
5. cd windows
6. make -f Makefile.mgw TOOLPATH=<your arch>-w64-mingw32- putty++.exe

#### Build and run unittests for SFTP backend and find:

1. Install the gdb Cygwin package.
2. cd windows/<sftp/find>/test
3. make -f Makefile.mgw TOOLPATH=i686-w64-mingw32- <sftp/find>test.exe
4. ./<sftp/find>test.exe
5. ../../memleak/memleak.sh <sftp/find>test.exe

The memleak.sh is a memory leak detector by catching the calls for malloc/realloc/free in msvcrt.dll using on gdb.
It will print the id-s of the aallocations which were not freed. You have to search the id-s in memleak.out file to see the call stack of the problematic allocations.

Using gdb for memory leak detection is a simple way but quite fragile.
Need to know where the malloc/realloc functions return, but unfortunately this depends on the exact version of msvcrt.dll.
This means most probably the memleak.gdb gdb script won't work out of the box for you.
You need to find the offset of 'ret' instuction of malloc/realloc using the disassembly feature of gdb and upadate the memleak.gdb.
There is a special testcase called tc_memleak_gdb to check if the offsets are right and the memleak.gdb works well.
Also currently memleak.gdb supports only i686 architecture and not x86_64.
