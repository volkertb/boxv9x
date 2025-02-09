@echo off

REM Assemble
ml -coff -DBLD_COFF -W2 -Zd -c -Cx -DMASM6 -Sg -DVGA -DVGA31 -DMINIVDD=1 -Foboxvmini.obj boxvmini.asm

REM Link
link /VXD /NOD boxvmini.obj /OUT:boxvmini.vxd /MAP:boxvmini.map /DEF:boxvmini.def
