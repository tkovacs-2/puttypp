# Putty++

Current features:
- Full knowledge of Putty 0.80
- Multiple terminal sessions in single frame window.
  No background Putty processes, no window embedding.
  Everything is handled by single process.
- Sign incoming data for not visible terminal sessions.
- Prevent accidental paste of big clipboard data.
- Embedded ConPTY backend from pterm.

Planned features:
- Find in terminal buffer.
- Syntax highlight.

Build:
This readme describes steps with MinGW-w64 in Cygwin environment.
Based on this and the original cmake files you can adapt for other environments.

1. Install the cmake, make, git and mingw64-<your arch>-gcc-core Cygwin packages.
2. Download Putty 0.80 source from https://the.earth.li/~sgtatham/putty/0.80/putty-0.80.tar.gz or
   through the release page https://www.chiark.greenend.org.uk/~sgtatham/putty/releases/0.80.html
3. Extract it into same folder like this readme file.
   A new folder name putty-0.80 should appear with the original Putty 0.80 source inside.
4. Remove the following files from the putty-0.80 folder:
     putty-0.80/be_list.c
     putty-0.80/windows/conpty.c
     putty-0.80/windows/dialog.c
     putty-0.80/windows/platform.h
     putty-0.80/windows/putty.c
     putty-0.80/windows/putty.rc
     putty-0.80/windows/utils/shinydialogbox.c
     putty-0.80/windows/version.rc2
     putty-0.80/windows/win-gui-seat.h
     putty-0.80/windows/window.c
5. cd windows
6. make -f Makefile.mgw TOOLPATH=i686-w64-mingw32- putty++.exe
