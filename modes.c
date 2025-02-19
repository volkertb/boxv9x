/*****************************************************************************

Copyright (c) 2022  Michal Necasek

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*****************************************************************************/

/* Display driver mode management. */

#include "winhack.h"
#include <gdidefs.h>
#include <dibeng.h>
#include <valmode.h>
#include <minivdd.h>
#include "minidrv.h"
#include "boxv.h"


/* Somewhat arbitrary max resolution. */
#define RES_MAX_X   (5 * 1024)
#define RES_MAX_Y   (5 * 768)


/* Generic DPMI calls, only used in this module. */
extern WORD DPMI_AllocLDTDesc( WORD cSelectors );
#pragma aux DPMI_AllocLDTDesc = \
    "xor    ax, ax"             \
    "int    31h"                \
    "jnc    OK"                 \
    "xor    ax, ax"             \
    "OK:"                       \
    parm [cx];

extern DWORD DPMI_GetSegBase( WORD Selector );
#pragma aux DPMI_GetSegBase =   \
    "mov    ax, 6"              \
    "int    31h"                \
    "mov    ax, cx"             \
    "xchg   ax, dx"             \
    parm [bx] modify [cx];

extern void DPMI_SetSegBase( WORD Selector, DWORD Base );
#pragma aux DPMI_SetSegBase =   \
    "mov    ax, 7"              \
    "int    31h"                \
    parm [bx] [cx dx];

extern void DPMI_SetSegLimit( WORD Selector, DWORD Limit );
#pragma aux DPMI_SetSegLimit =   \
    "mov    ax, 8"              \
    "int    31h"                \
    parm [bx] [cx dx];

/* NB: Compiler insists on CX:BX and DI:SI, DPMI needs it word swapped. */
extern DWORD DPMI_MapPhys( DWORD Base, DWORD Size );
#pragma aux DPMI_MapPhys =      \
    "xchg   cx, bx"             \
    "xchg   si, di"             \
    "mov    ax, 800h"           \
    "int    31h"                \
    "jnc    OK"                 \
    "xor    bx, bx"             \
    "xor    cx, cx"             \
    "OK:"                       \
    "mov    dx, bx"             \
    "mov    ax, cx"             \
    parm [cx bx] [di si];


WORD wScreenX       = 0;
WORD wScreenY       = 0;
WORD BitBltDevProc  = 0;
WORD ScreenSelector = 0;
WORD wPDeviceFlags  = 0;

static DWORD    dwScreenFlatAddr = 0;   /* 32-bit flat address of VRAM. */
static DWORD    dwVideoMemorySize = 0;  /* Installed VRAM in bytes. */
static WORD     wScreenPitchBytes = 0;  /* Current scanline pitch. */
static DWORD    dwPhysVRAM = 0;         /* Physical LFB base address. */

/* These are currently calculated not needed in the absence of
 * offscreen video memory.
 */
static WORD wMaxWidth  = 0;
static WORD wMaxHeight = 0;

/* On Entry:
 * EAX   = Function code (VDD_DRIVER_REGISTER)
 * EBX   = This VM's handle
 * ECX   = Size of all visible scanlines in bytes
 * EDX   = Zero to tell VDD to try virtualizing
 * ES:DI = Pointer to function called when switching
 *         back from fullscreen.
 *
 * On Return:
 * EAX   = Amount of video memory used by VDD in bytes,
 *         or function code if VDD call failed.
 */
extern DWORD CallVDDRegister( WORD Function, WORD wPitch, WORD wHeight, void _far *fRHR );
#pragma aux CallVDDRegister =       \
    ".386"                          \
    "movzx  eax, ax"                \
    "movzx  edx, dx"                \
    "mul    edx"                    \
    "mov    ecx, eax"               \
    "movzx  eax, bx"                \
    "movzx  ebx, OurVMHandle"       \
    "xor    edx, edx"               \
    "call   dword ptr VDDEntryPoint"\
    "mov    edx, eax"               \
    "shr    edx, 16"                \
    parm [bx] [ax] [dx] [es di];


#pragma code_seg( _INIT );

/* Take a mode descriptor and change it to be a valid
 * mode if it isn't already. Return zero if mode needed
 * fixing (wasn't valid).
 */
WORD FixModeInfo( LPMODEDESC lpMode )
{
    WORD    rc = 1; /* Assume valid mode. */

    /* First validate bits per pixel. */
    switch( lpMode->bpp ) {
    case 8:
    case 16:
    case 24:
    case 32:
        break;
    default:
        lpMode->bpp = 8;    /* Default to 8 bpp. */
        rc = 0;             /* Mode wasn't valid. */
    }

    /* Validate mode. If resolution is under 640x480 in
     * either direction, force 640x480.
     */
    if( lpMode->xRes < 640 || lpMode->yRes < 480 )
    {
        lpMode->xRes = 640; /* Force 640x480. */
        lpMode->yRes = 480;
        rc = 0;             /* Mode wasn't valid. */
    }

    /* Clip the resolution to something that probably won't make
     * Windows have a cow.
     */
    if( lpMode->xRes > RES_MAX_X ) {
        lpMode->xRes = RES_MAX_X;
        rc = 0;
    }
    if( lpMode->yRes > RES_MAX_Y ) {
        lpMode->yRes = RES_MAX_Y;
        rc = 0;
    }

    return( rc );
}


/* Calculate pitch for a given horizontal resolution and bpp. */
WORD CalcPitch( WORD x, WORD bpp )
{
    WORD    wPitch;

    /* Valid BPP must be a multiple of 8 so it's simple. */
    wPitch = x * (bpp / 8);

    /* Align to 32 bits. */
    wPitch = (wPitch + 3) & ~3;

    return( wPitch );
}


/* Return non-zero if given mode is supported. */
static int IsModeOK( WORD wXRes, WORD wYRes, WORD wBpp )
{
    MODEDESC    mode;
    DWORD       dwModeMem;

    mode.bpp  = wBpp;
    mode.xRes = wXRes;
    mode.yRes = wYRes;

    /* If mode needed fixing, it's not valid. */
    if( !FixModeInfo( &mode ) )
        return( 0 );

    /* Make sure there's enough VRAM for it. */
    dwModeMem = (DWORD)CalcPitch( wXRes, wBpp ) * wYRes;
    if( dwModeMem > dwVideoMemorySize )
        return( 0 );

    return( 1 );
}


/* Clear the visible screen by setting it to all black (zeros).
 * NB: Assumes there is no off-screen region to the right of
 * the visible area.
 */
static void ClearVisibleScreen( void )
{
    LPDWORD     lpScr;
    WORD        wLines = wScreenY;
    WORD        i;

    lpScr = ScreenSelector :> 0;

    while( wLines-- ) {
        for( i = 0; i < wScreenPitchBytes / 4; ++i )
            lpScr[i] = 0;
        lpScr += wScreenPitchBytes; /* Scanline pitch. */
    }
}


static DWORD AllocLinearSelector( DWORD dwPhysAddr, DWORD dwSize )
{
    WORD    wSel;
    DWORD   dwLinear;

    wSel = DPMI_AllocLDTDesc( 1 );  /* One descriptor, please. */
    if( !wSel )
        return( 0 );

    /* Map the framebuffer physical memory. */
    dwLinear = DPMI_MapPhys( dwPhysAddr, dwSize );

    /* Now set the allocated selector to point to VRAM. */
    DPMI_SetSegBase( wSel, dwLinear );
    DPMI_SetSegLimit( wSel, dwSize - 1 );

    return( wSel );
}


/* Set the currently configured mode (wXRes/wYRes) in hardware.
 * If bFullSet is non-zero, then also reinitialize globals.
 * When re-establishing a previously set mode (e.g. coming
 * back from fullscreen), bFullSet will be zero.
 * NB: BPP won't change at runtime.
 */
static int SetDisplayMode( WORD wXRes, WORD wYRes, int bFullSet )
{
    dbg_printf( "SetDisplayMode: wXRes=%u wYRes=%u\n", wXRes, wYRes );

    /* Inform the VDD that the mode is about to change. */
    CallVDD( VDD_PRE_MODE_CHANGE );

    BOXV_ext_mode_set( 0, wXRes, wYRes, wBpp, wXRes, wYRes );

    if( bFullSet ) {
        wScreenX = wXRes;
        wScreenY = wYRes;

        wScreenPitchBytes = CalcPitch( wXRes, wBpp );
        BitBltDevProc     = NULL;       /* No acceleration implemented. */
        wPDeviceFlags     = MINIDRIVER | VRAM;
        if( wBpp == 16 ) {
            wPDeviceFlags |= FIVE6FIVE; /* Needed for 16bpp modes. */
        }

        wMaxWidth  = wScreenPitchBytes / (wBpp / 8);    /* We know bpp is a multiple of 8. */
        wMaxHeight = dwVideoMemorySize / wScreenPitchBytes;

        /* Offscreen regions could be calculated here. We do not use those. */
    }
    return( 1 );
}


/* Forward declaration. */
void __far RestoreDesktopMode( void );

int PhysicalEnable( void )
{
    DWORD   dwRegRet;

    if( !ScreenSelector ) {
        int     iChipID;

        /* Extra work if driver hasn't yet been initialized. */
        iChipID = BOXV_detect( 0, &dwVideoMemorySize );
        if( !iChipID ) {
            return( 0 );
        }
        dwPhysVRAM = LfbBase;
        dbg_printf( "PhysicalEnable: Hardware detected, dwVideoMemorySize=%lX dwPhysVRAM=%lX\n", dwVideoMemorySize, dwPhysVRAM );
    }

    if( !IsModeOK( wScrX, wScrY, wBpp ) ) {
        /* Can't find mode, oopsie. */
        dbg_printf( "PhysicalEnable: Mode not valid! wScrX=%u wScrY=%u wBpp=%u\n", wScrX, wScrY, wBpp );
        return( 0 );
    }

    if( !SetDisplayMode( wScrX, wScrY, 1 ) ) {
        /* This is not good. */
        dbg_printf( "PhysicalEnable: SetDisplayMode failed! wScrX=%u wScrY=%u wBpp=%u\n", wScrX, wScrY, wBpp );
        return( 0 );
    }

    /* Allocate an LDT selector for the screen. */
    if( !ScreenSelector ) {
        ScreenSelector = AllocLinearSelector( dwPhysVRAM, dwVideoMemorySize );
        if( !ScreenSelector ) {
            dbg_printf( "PhysicalEnable: AllocScreenSelector failed!\n" );
            return( 0 );
        }
    }

    /* NB: Currently not used. DirectDraw would need the segment base. */
    dwScreenFlatAddr = DPMI_GetSegBase( ScreenSelector );   /* Not expected to fail. */

    dbg_printf( "PhysicalEnable: RestoreDesktopMode is at %WP\n", RestoreDesktopMode );
    dwRegRet = CallVDDRegister( VDD_DRIVER_REGISTER, wScreenPitchBytes, wScreenY, RestoreDesktopMode );
    if( dwRegRet != VDD_DRIVER_REGISTER ) {
        /* NB: It's not fatal if CallVDDRegister() fails. */
        /// @todo What can we do with the returned value?
    }

    /* Let the VDD know that the mode changed. */
    CallVDD( VDD_POST_MODE_CHANGE );
    CallVDD( VDD_SAVE_DRIVER_STATE );

    ClearVisibleScreen();

    return( 1 );    /* All good. */
}


/* Check if the requested mode can be set. Return yes, no (with a reason),
 * or maybe.                                                           .
 * Must be exported by name, recommended ordinal 700.
 * NB: Can be called when the driver is not the current display driver.
 */
#pragma aux ValidateMode loadds;    /* __loadds not operational due to prototype in valmode.h */
UINT WINAPI __loadds ValidateMode( DISPVALMODE FAR *lpValMode )
{
    UINT        rc = VALMODE_YES;

    dbg_printf( "ValidateMode: X=%u Y=%u bpp=%u\n", lpValMode->dvmXRes, lpValMode->dvmYRes, lpValMode->dvmBpp );
    do {
        if( !ScreenSelector ) {
            int     iChipID;

            /* Additional checks if driver isn't running. */
            iChipID = BOXV_detect( 0, &dwVideoMemorySize );
            if( !iChipID ) {
                rc = VALMODE_NO_WRONGDRV;
                break;
            }
            dwPhysVRAM = LfbBase;
            dbg_printf( "ValidateMode: Hardware detected, dwVideoMemorySize=%lX dwPhysVRAM=%lX\n", dwVideoMemorySize, dwPhysVRAM );
        }

        if( !IsModeOK( lpValMode->dvmXRes, lpValMode->dvmYRes, lpValMode->dvmBpp ) ) {
            rc = VALMODE_NO_NOMEM;
        }
    } while( 0 );

    dbg_printf( "ValidateMode: rc=%u\n", rc );
    return( rc );
}

#pragma code_seg( _TEXT );

/* Called by the VDD in order to restore the video state when switching back
 * to the system VM. Function is in locked memory (_TEXT segment).
 * NB: Must reload DS, but apparently need not save/restore DS.
 */
#pragma aux RestoreDesktopMode loadds;
void __far RestoreDesktopMode( void )
{
    dbg_printf( "RestoreDesktopMode: %ux%u, wBpp=%u\n", wScreenX, wScreenY, wBpp );

    /* Set the current desktop mode again. */
    SetDisplayMode( wScreenX, wScreenY, 0 );

    /* Reprogram the DAC if relevant. */
    if( wBpp <= 8 ) {
        UINT    wPalCnt;

        switch( wBpp ) {
        case 8:
            wPalCnt = 256;
            break;
        case 4:
            wPalCnt = 16;
            break;
        default:
            wPalCnt = 2;
            break;
        }
        SetRAMDAC_far( 0, wPalCnt, lpColorTable );
    }

    /* Clear the busy flag. Quite important. */
    lpDriverPDevice->deFlags &= ~BUSY;

    /* Poke the VDD now that everything is restored. */
    CallVDD( VDD_SAVE_DRIVER_STATE );

    ClearVisibleScreen();
}

