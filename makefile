OBJS = dibthunk.obj dibcall.obj enable.obj init.obj palette.obj &
       scrsw.obj sswhook.obj modes.obj boxv.obj

INCS = -I$(%WATCOM)\h\win -Iddk

# Define HWBLT if BitBlt can be accelerated.
#FLAGS = -DHWBLT

# Set DBGPRINT to add debug printf logging.
# DBGPRINT = 1
!ifdef DBGPRINT
FLAGS += -DDBGPRINT
OBJS  += dbgprint.obj
# Need this to work with pre-made boxv9x.lnk
DBGFILE = file dbgprint.obj
!else
DBGFILE =
!endif

boxvmini.drv : $(OBJS) display.res dibeng.lib boxv9x.lnk
	wlink op quiet, start=DriverInit_ disable 2055 @boxv9x.lnk $(DBGFILE)
	wrc -q display.res $@

# Linker script
boxv9x.lnk : boxv9x.def
        ms2wlink $(OBJS),boxvmini.drv,boxvmini.map,dibeng.lib clibs.lib,boxv9x.def > boxv9x.lnk

# Object files
boxv.obj : boxv.c .autodepend
	wcc -q -wx -s -zu -zls -3 $(FLAGS) $<

dbgprint.obj : dbgprint.c .autodepend
	wcc -q -wx -s -zu -zls -3 $(FLAGS) $<

dibcall.obj : dibcall.c .autodepend
	wcc -q -wx -s -zu -zls -3 -zW $(INCS) $(FLAGS) $<

dibthunk.obj : dibthunk.asm
	wasm -q $(FLAGS) $<

enable.obj : enable.c .autodepend
	wcc -q -wx -s -zu -zls -3 -zW $(INCS) $(FLAGS) $<

init.obj : init.c .autodepend
	wcc -q -wx -s -zu -zls -3 -zW $(INCS) $(FLAGS) $<

palette.obj : palette.c .autodepend
	wcc -q -wx -s -zu -zls -3 -zW $(INCS) $(FLAGS) $<

sswhook.obj : sswhook.asm
	wasm -q $(FLAGS) $<

modes.obj : modes.c .autodepend
	wcc -q -wx -s -zu -zls -3 -zW $(INCS) $(FLAGS) $<

scrsw.obj : scrsw.c .autodepend
	wcc -q -wx -s -zu -zls -3 -zW $(INCS) $(FLAGS) $<

# Resources
display.res : res/display.rc res/colortab.bin res/config.bin res/fonts.bin res/fonts120.bin .autodepend
	wrc -q -r -ad -bt=windows -fo=$@ -Ires -I$(%WATCOM)/h/win res/display.rc

res/colortab.bin : res/colortab.c
	wcc -q $(INCS) $<
	wlink op quiet disable 1014, 1023 name $@ sys dos output raw file colortab.obj

res/config.bin : res/config.c
	wcc -q $(INCS) $<
	wlink op quiet disable 1014, 1023 name $@ sys dos output raw file config.obj

res/fonts.bin : res/fonts.c .autodepend
	wcc -q $(INCS) $<
	wlink op quiet disable 1014, 1023 name $@ sys dos output raw file fonts.obj

res/fonts120.bin : res/fonts120.c .autodepend
	wcc -q $(INCS) $<
	wlink op quiet disable 1014, 1023 name $@ sys dos output raw file fonts120.obj

# Libraries
dibeng.lib : ddk/dibeng.lbc
	wlib -b -q -n -fo -ii @$< $@

# Cleanup
clean : .symbolic
    rm *.obj
    rm *.err
    rm *.lib
    rm *.drv
    rm *.map
    rm *.res
    rm *.img
    rm res/*.obj
    rm res/*.bin

image : .symbolic boxv9x.img

# Create a 1.44MB distribution floppy image.
# NB: The mkimage tool is not supplied.
boxv9x.img : boxvmini.drv boxv9x.inf readme.txt
    if not exist dist mkdir dist
    copy boxvmini.drv dist
    copy boxv9x.inf   dist
    copy readme.txt   dist
    mkimage -l BOXV9X -o boxv9x.img dist
