@echo off

setlocal

del .dbg\*.pdb > NUL 2> NUL
del .dbg\*.map  NUL 2> NUL
del .bin\*.exe > NUL 2> NUL
del .objs\*.obj > NUL 2> NUL