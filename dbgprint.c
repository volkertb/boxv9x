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

/* Homegrown printf support. The C runtime printf() is hard to use without
 * dragging in much of the library, and much worse, the small model runtime                                                                              .
 * can't operate with SS != DS. So we roll our own very simplified printf()                                                                                    .
 * subset, good enough for what we need.                                                                            .
 */

#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <conio.h>


/* Backdoor logging I/O ports. */
#define INFO_PORT   0xe9

#pragma code_seg( _INIT );

static void prt_ch( char c )
{
    outp( INFO_PORT, c );
}

static void prt_u32( uint32_t val, int width )
{
    uint32_t    uval;
    uint32_t    base;
    char        buf[16];    /* It's 12 digits max. */
    int         n = 0;

    uval = val;

    do {
        base = uval / 10;
        buf[n++] = uval - base * 10 + '0';
        uval = base;
    } while( uval );

    /* Pad with spaces. */
    while( width > n ) {
        prt_ch( ' ' );
        --width;
    }

    do {
        prt_ch( buf[--n] );
    } while( n );
}

static void prt_i32( int32_t val, int width )
{
    uint32_t    uval;

    if( val < 0 ) {
        prt_ch( '-' );
        uval = -val;
        if( width > 0 )
            --width;
    } else {
        uval = val;
    }

    prt_u32( uval, width );
}

static void prt_hex32( uint32_t val, int width, char hexa )
{
    char    buf[8]; /* Enough for a dword. */
    int     n = 0;
    int     nibble;

    do {
        nibble = val & 0xF;
        val >>= 4;
        buf[n++] = nibble + (nibble > 9 ? hexa - 10 : '0');
    } while( val );

    /* Pad with zeros. */
    while( width > n ) {
        prt_ch( '0' );
        --width;
    }

    do {
        prt_ch( buf[--n] );
    } while( n );
}

static void prt_str( const char __far *s )
{
    while( *s )
        prt_ch( *s++ );
}

void dbg_printf( const char *s, ... )
{
    char        type_len;
    char        conv;
    char        hexa;
    int         width;
    uint16_t    word;
    uint32_t    dword;
    va_list     args;

    va_start( args, s );

    /* Go until end of format string. */
    while( *s ) {
        /* Is it a format specifier? */
        if( *s == '%' ) {
            ++s;    /* Eat the % sign. */
            conv     = 0;
            width    = 0;
            type_len = 0;

            /* Process field width, if any. */
            while( isdigit( *s ) ) {
                width *= 10;
                width += *s++ - '0';
            }

            /* Process type length specifier, if any. */
            switch( *s ) {
            case 'l':   /* Long integers. */
            case 'W':   /* Far pointers. */
                type_len = *s++;
                break;
            default:    /* Do nothing. */
                break;
            }

            /* Now check if a supported conversion is there. */
            switch( *s ) {
            case 'c':
            case 'd':
            case 'u':
            case 'p':
            case 'P':
            case 's':
            case 'x':
            case 'X':
                conv = *s++;
                break;
            default:    /* Do nothing. */
                break;
            }

            if( conv ) {
                /* Time to start grabbing stuff off the stack. */
                word = va_arg( args, uint16_t );
                /* If argument is double wide, build a doubleword. */
                if( type_len == 'l' || type_len == 'W' ) {
                    dword = va_arg( args, uint16_t );
                    dword <<= 16;
                    dword |= word;
                }
                if( conv == 'c' ) {
                    prt_ch( word );
                } else if( conv == 'd' ) {
                    if( type_len == 'l' )
                        prt_i32( dword, width );
                    else
                        prt_i32( (int16_t)word, width );
                } else if( conv == 'u' ) {
                    if( type_len == 'l' )
                        prt_u32( dword, width );
                    else
                        prt_u32( word, width );
                } else if( conv == 'p' || conv == 'P' ) {
                    hexa = conv - 'P' + 'A';
                    if( type_len == 'W' ) {
                        prt_hex32( dword >> 16, 4, hexa );
                        prt_ch( ':' );
                        prt_hex32( word, 4, hexa );
                    } else {
                        prt_hex32( word, 4, hexa );
                    }
                } else if( conv == 's' ) {
                    if( type_len == 'W' )
                        prt_str( (const char __far *)dword );
                    else
                        prt_str( (const char *)word );
                } else if( conv == 'x' || conv == 'X' ) {
                    hexa = conv - 'X' + 'A';
                    if( type_len == 'l' ) {
                        if( !width )
                            width = 8;
                        prt_hex32( dword, width, hexa );
                    } else {
                        if( !width )
                            width = 4;
                        prt_hex32( word, width, hexa );
                    }
                }
            } else {
                /* Just print whatever is there. */
                prt_ch( *s++ );
            }
        } else {
            prt_ch( *s++ );
        }
    }
    va_end( args );
}
