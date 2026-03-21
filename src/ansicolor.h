#pragma once

// ============================================================================
// ansicolor.h  --  ANSI 24-bit RGB color macros  [narrow string / char]
//
// All macros expand to narrow "..." string literals for use with printf.
//
// Usage:
//   printf(AC_OOM "Out of memory\n" AC_RESET "\n");
// ============================================================================

#define AC_ESC       "\x1b["
#define AC_RESET     AC_ESC "0m"
#define AC_BOLD      AC_ESC "1m"
#define AC_DIM       AC_ESC "2m"
#define AC_ITALIC    AC_ESC "3m"
#define AC_UNDERLINE AC_ESC "4m"
#define AC_BLINK     AC_ESC "5m"
#define AC_REVERSE   AC_ESC "7m"
#define AC_STRIKE    AC_ESC "9m"

// ---- Semantic aliases (direct narrow literals) ------------------------------

// Timestamp         : soft cyan
#define AC_TIMESTAMP    AC_ESC "1m" AC_ESC "38;2;100;200;210m"

// Header / border   : vivid blue-purple
#define AC_HEADER       AC_ESC "1m" AC_ESC "38;2;120;130;255m"

// OK / success      : mint green
#define AC_OK           AC_ESC "38;2;80;230;160m"

// Numbers           : yellow
#define AC_NUM          AC_ESC "38;2;255;220;60m"

// Address           : light orange
#define AC_ADDR         AC_ESC "38;2;255;165;80m"

// Warning           : deep orange
#define AC_WARN         AC_ESC "1m" AC_ESC "38;2;255;140;0m"

// OOM event         : vivid red
#define AC_OOM          AC_ESC "1m" AC_ESC "38;2;255;60;60m"

// Exception name    : red-purple
#define AC_EXCNAME      AC_ESC "1m" AC_ESC "38;2;255;80;180m"

// Exception code    : soft red
#define AC_EXCCODE      AC_ESC "38;2;230;100;100m"

// VEH / SEH label   : purple
#define AC_VEH          AC_ESC "1m" AC_ESC "38;2;200;100;255m"

// Fallsafe          : deep red on dark red background
#define AC_FALLSAFE     AC_ESC "1m" AC_ESC "38;2;255;50;50m" AC_ESC "48;2;60;0;0m"

// System mem label  : light blue
#define AC_SYSKEY       AC_ESC "38;2;130;190;230m"

// System mem value  : silver-white
#define AC_SYSVAL       AC_ESC "38;2;210;220;235m"

// Process mem label : light green
#define AC_PROCKEY      AC_ESC "38;2;130;220;160m"

// Memory load high (>=75%)
#define AC_MEM_HIGH     AC_ESC "1m" AC_ESC "38;2;255;80;80m"
// Memory load mid  (50-75%)
#define AC_MEM_MID      AC_ESC "38;2;255;200;60m"
// Memory load low  (<50%)
#define AC_MEM_LOW      AC_ESC "38;2;80;220;120m"

// Phase heading     : yellow-green
#define AC_PHASE        AC_ESC "38;2;180;230;100m"

// Scenario number   : cyan
#define AC_SCENARIO     AC_ESC "1m" AC_ESC "38;2;60;210;220m"

// Runtime log       : grey
#define AC_RUNTIME      AC_ESC "38;2;160;165;180m"

// Watchdog          : amber
#define AC_WATCHDOG     AC_ESC "1m" AC_ESC "38;2;255;190;40m"


// Completion        : bright green
#define AC_SUCCESS      AC_ESC "1m" AC_ESC "38;2;60;240;120m"

// Invalid command   : dark grey
#define AC_INVALID      AC_ESC "38;2;160;160;160m"

// Menu title        : gold
#define AC_TITLE        AC_ESC "1m" AC_ESC "38;2;255;210;60m"

// Menu item number  : coral
#define AC_ITEM_NUM     AC_ESC "38;2;255;120;80m"

// Menu item text    : lavender
#define AC_ITEM_TEXT    AC_ESC "38;2;200;195;240m"

// Prompt            : white
#define AC_PROMPT       AC_ESC "1m" AC_ESC "38;2;240;240;240m"

// ---- VT mode + UTF-8 console setup -----------------------------------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

namespace Ansi
{
    // Enable VT sequences on stdout/stderr and set console to UTF-8.
    // After this call printf outputs UTF-8 to the Windows console correctly.
    inline void EnableVT()
    {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        auto enable = [](DWORD h)
        {
            HANDLE hnd = GetStdHandle(h);
            if (!hnd || hnd == INVALID_HANDLE_VALUE) return;
            DWORD mode = 0;
            if (!GetConsoleMode(hnd, &mode)) return;
            SetConsoleMode(hnd, mode
                | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                | DISABLE_NEWLINE_AUTO_RETURN);
        };
        enable(STD_OUTPUT_HANDLE);
        enable(STD_ERROR_HANDLE);
    }
}