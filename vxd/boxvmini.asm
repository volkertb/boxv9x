;
; Mini-VDD VxD for the BOXV9X display driver on QEMU
;
; Having this mini-VDD present fixes a couple of problems with the BOXV9X
; driver. First, it virtualizes accesses to the Bochs VGA registers on I/O
; ports 1CEh and 1CFh (VBE_DISPI_IOPORT_INDEX and VBE_DISPI_IOPORT_DATA).
; This means that when you start an MS-DOS Prompt, the screen doesn't go
; wonky when the DOS VM, calling into the VGA ROM BIOS, ends up trying to
; perform I/O to those ports.
;
; Second, it fixes a problem at Windows shutdown (under KVM and WHPX accel)
; where the machine hangs instead of showing the "Windows is shutting down"
; graphic momentarily, and then actually shutting down.
;
.386p

.xlist
include VMM.INC
include MINIVDD.INC
.list

Declare_Virtual_Device \
    BOXVMINI, 4, 0, \
    MiniVDD_Control, \
    Undefined_Device_ID, \
    VDD_Init_Order,,,,

; Data segment
VxD_DATA_SEG

WindowsVMHandle     dd ?

VxD_DATA_ENDS

; Init segment (discardable)
VxD_ICODE_SEG

public  MiniVDD_Dynamic_Init
BeginProc MiniVDD_Dynamic_Init
;
    mov WindowsVMHandle, ebx
;
    VxDCall VDD_Get_Mini_Dispatch_Table
    MiniVDDDispatch PRE_HIRES_TO_VGA, PreHiResToVGA
    MiniVDDDispatch POST_HIRES_TO_VGA, PostHiResToVGA
    MiniVDDDispatch ENABLE_TRAPS, EnableTraps
    MiniVDDDispatch DISPLAY_DRIVER_DISABLING, DisplayDriverDisabling
;
    mov esi, OFFSET32 MiniVDD_Virtual1CE
    mov edx, 1ceh
    VMMCall Install_IO_Handler
    mov edx, 1ceh
    VMMCall Disable_Global_Trapping
;
    mov esi, OFFSET32 MiniVDD_Virtual1CE
    mov edx, 1cfh
    VMMCall Install_IO_Handler
    mov edx, 1cfh
    VMMCall Disable_Global_Trapping
;
    clc
    ret
EndProc MiniVDD_Dynamic_Init

public  MiniVDD_Init_Complete
BeginProc MiniVDD_Init_Complete
;
; At Windows shutdown time, the display driver calls VDD_DRIVER_UNREGISTER,
; which calls our DisplayDriverDisabling callback, and soon thereafter
; does an INT 10h (AH=0) to mode 3 (and then 13) in V86 mode. However, the
; memory ranges for display memory are not always mapped!
;
; If the VGA ROM BIOS, during execution of such a set video mode call, tries to
; clear the screen, and the appropriate memory range isn't mapped, then we end
; up in a page fault handler, which I guess maps the memory and then resumes
; execution in V86 mode. But strangely, in QEMU, this fault & resume mechanism
; does not work with KVM or WHPX assist, at least on Intel. The machine hangs
; instead (I have not root caused why).
;
; We can dodge this problem by setting up real mappings up front.
;
; At entry, EBX contains a Windows VM handle.
;
    mov edx, 0A0h
    VMMCall _PhysIntoV86,<edx,ebx,edx,16,0>
;
    mov edx, 0B8h
    VMMCall _PhysIntoV86,<edx,ebx,edx,8,0>
;
    ret
EndProc MiniVDD_Init_Complete

VxD_ICODE_ENDS

; Fixed segment
VxD_LOCKED_CODE_SEG

Begin_Control_Dispatch MiniVDD
    Control_Dispatch Device_Init, MiniVDD_Dynamic_Init
    Control_Dispatch Init_Complete, MiniVDD_Init_Complete
    Control_Dispatch Sys_Dynamic_Device_Init, MiniVDD_Dynamic_Init
End_Control_Dispatch MiniVDD

public  MiniVDD_Virtual1CE
BeginProc MiniVDD_Virtual1CE
; AX/AL contains the value to be read or written on the port.
; EBX contains the handle of the VM accessing the port.
; ECX contains the direction (in/out) and size (byte/word) of the operation.
; EDX contains the port number, which for us will either be 1CEh or 1CFh.
    VxDCall VDD_Get_VM_Info
    cmp edi, WindowsVMHandle    ; Is the CRTC controlled by Windows?
    jne _Virtual1CEPhysical     ; If not, we should allow the I/O
    cmp ebx, edi                ; Is the calling VM Windows?
    jne _Virtual1CEExit         ; If not, we should eat the I/O
_Virtual1CEPhysical:
    VxDJmp VDD_Do_Physical_IO
_Virtual1CEExit:
    ret
EndProc MiniVDD_Virtual1CE

public  MiniVDD_EnableTraps
BeginProc MiniVDD_EnableTraps
    mov edx, 1ceh
    VMMCall Enable_Global_Trapping
    mov edx, 1cfh
    VMMCall Enable_Global_Trapping
    ret
EndProc MiniVDD_EnableTraps

public  MiniVDD_PreHiResToVGA
BeginProc MiniVDD_PreHiResToVGA
    mov edx, 1ceh
    VMMCall Disable_Global_Trapping
    mov edx, 1cfh
    VMMCall Disable_Global_Trapping
    ret
EndProc MiniVDD_PreHiResToVGA

public  MiniVDD_PostHiResToVGA
BeginProc MiniVDD_PostHiResToVGA
    mov edx, 1ceh
    VMMCall Enable_Global_Trapping
    mov edx, 1cfh
    VMMCall Enable_Global_Trapping
    ret
EndProc MiniVDD_PostHiResToVGA

public  MiniVDD_DisplayDriverDisabling
BeginProc MiniVDD_DisplayDriverDisabling
    mov edx, 1ceh
    VMMCall Disable_Global_Trapping
    mov edx, 1cfh
    VMMCall Disable_Global_Trapping
    ret
EndProc MiniVDD_DisplayDriverDisabling

VxD_LOCKED_CODE_ENDS
end
