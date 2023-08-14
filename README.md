# Putty++

Current features:
- Full knowledge of Putty 0.76
- Multiple terminal sessions in single frame window.
  No background Putty processes, no window embedding.
  Everything is handled by single process.
- Sign incoming data for not visible terminal sessions.

Planned features:
- Update to latest Putty source.
- Prevent accidental paste of big clipboard data.
- Find in terminal buffer.
- Syntax highlight.

Build:
This readme describes steps with MinGW-w64 in Cygwin environment.
Based on this and the original project files you can adapt for other environments.

1. Install the make and mingw64-<your arch>-gcc-core Cygwin packages.
2. Download Putty 0.76 source from https://the.earth.li/~sgtatham/putty/0.76/putty-0.76.tar.gz or
   through the release page https://www.chiark.greenend.org.uk/~sgtatham/putty/releases/0.76.html
3. Extract it into same folder like this readme file.
   A new folder name putty-0.76 should appear with the original Putty 0.76 source inside.
4. Remove the following files from the putty-0.76 folder:
     putty-0.76/be_all_s.c
     putty-0.76/windows/putty.rc
     putty-0.76/windows/version.rc2
     putty-0.76/windows/windlg.c
     putty-0.76/windows/window.c
     putty-0.76/windows/winseat.h
     putty-0.76/windows/winstuff.h
     putty-0.76/windows/win_res.rc2
5. cd windows
6. make -f Makefile.mgw TOOLPATH=i686-w64-mingw32- putty++.exe
