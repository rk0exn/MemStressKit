#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include "reporter.h"
#include "ansicolor.h"

#pragma comment(lib, "psapi.lib")

// ---------------------------------------------------------------------------
// SafeWrite: WriteFile to stdout without touching the CRT heap.
// Used inside VEH/SEH where the heap may be exhausted or corrupt.
// ---------------------------------------------------------------------------
static void SafeWrite(const char* msg)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
              msg, static_cast<DWORD>(strlen(msg)), &written, nullptr);
}

static void Timestamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::printf(
        AC_TIMESTAMP "[%02d:%02d:%02d.%03d]" AC_RESET " ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

// Heap-free timestamp written via WriteFile (safe during OOM/corruption).
static void SafeTimestamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    // wsprintfA: no CRT heap usage
    wsprintfA(buf, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    SafeWrite(buf);
}

static const char* ExceptionName(DWORD code)
{
    switch (code)
    {
    case STATUS_NO_MEMORY:                return "STATUS_NO_MEMORY";
    case 0xC0000374UL:                    return "STATUS_HEAP_CORRUPTION";
    case STATUS_ACCESS_VIOLATION:         return "STATUS_ACCESS_VIOLATION";
    case STATUS_STACK_OVERFLOW:           return "STATUS_STACK_OVERFLOW";
    case static_cast<DWORD>(0xE06D7363): return "C++ EH (bad_alloc)";
    default:                              return "UNKNOWN";
    }
}

static const char* MemLoadColor(DWORD pct)
{
    if (pct >= 75) return AC_MEM_HIGH;
    if (pct >= 50) return AC_MEM_MID;
    return AC_MEM_LOW;
}

// ---------------------------------------------------------------------------
void Reporter::Init()
{
#ifdef _DEBUG
    const char* buildTag = "[DEBUG  ]";
#else
    const char* buildTag = "[RELEASE]";
#endif
    std::printf(
        AC_HEADER
        "+===============================================+\n"
        "|   MemStresser v2   PID=" AC_NUM "%-8lu" AC_HEADER
        "   %s   |\n"
        "+===============================================+"
        AC_RESET "\n\n",
        GetCurrentProcessId(), buildTag);
    std::fflush(stdout);
}

void Reporter::PrintSystemMemory()
{
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(),
                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                         sizeof(pmc));

    const DWORD load      = ms.dwMemoryLoad;
    const char* loadColor = MemLoadColor(load);

    Timestamp();
    std::printf(
        AC_SYSKEY "SYS" AC_RESET
        "  Load=" AC_BOLD "%s%lu%%" AC_RESET
        "  Avail=" AC_SYSVAL "%llu" AC_RESET "/" AC_SYSVAL "%llu"
        AC_SYSKEY "MB" AC_RESET
        "  VirtFree=" AC_SYSVAL "%llu" AC_SYSKEY "MB" AC_RESET "\n",
        loadColor, load,
        ms.ullAvailPhys >> 20, ms.ullTotalPhys >> 20,
        ms.ullAvailVirtual >> 20);

    std::printf(
        "         "
        AC_PROCKEY "PROC" AC_RESET
        "  WS=" AC_NUM "%llu" AC_PROCKEY "MB" AC_RESET
        "  Private=" AC_NUM "%llu" AC_PROCKEY "MB" AC_RESET
        "  PeakWS=" AC_NUM "%llu" AC_PROCKEY "MB" AC_RESET "\n",
        pmc.WorkingSetSize     >> 20,
        pmc.PrivateUsage       >> 20,
        pmc.PeakWorkingSetSize >> 20);
    std::fflush(stdout);
}

void Reporter::OnNewHandlerFired()
{
    // new handler fires before bad_alloc is thrown; heap is strained but
    // not necessarily corrupt yet -- printf is still safe here.
    Timestamp();
    std::printf(AC_OOM ">> new handler fired" AC_RESET " -- malloc/new failed\n");
    std::fflush(stdout);
    PrintSystemMemory();
}

void Reporter::OnVEH(EXCEPTION_POINTERS* ep)
{
    const DWORD code = ep->ExceptionRecord->ExceptionCode;

    // Use only heap-free I/O: WriteFile + wsprintfA.
    // printf's internal buffer allocation can fail or deadlock when the
    // heap is exhausted (STATUS_NO_MEMORY) or corrupt (STATUS_HEAP_CORRUPTION).
    SafeTimestamp();
    SafeWrite(AC_VEH "> VEH " AC_RESET);
    SafeWrite(ExceptionName(code));
    char buf[32];
    wsprintfA(buf, " (0x%08lX)\n", code);
    SafeWrite(buf);
    // Skip PrintSystemMemory here -- GlobalMemoryStatusEx is safe but
    // the subsequent printf inside it is not.  The debugger's memory
    // window is more reliable at this point anyway.
}

LONG Reporter::SEHFilter(DWORD code, EXCEPTION_POINTERS* ep)
{
    // Heap-free output only: the SEH filter expression is evaluated while
    // the exception is still unwinding through ntdll.
    SafeTimestamp();
    SafeWrite(AC_VEH "> SEH " AC_RESET);
    SafeWrite(ExceptionName(code));
    char buf[64];
    if (ep && ep->ExceptionRecord)
        wsprintfA(buf, " (0x%08lX) @ %p\n",
                  code, ep->ExceptionRecord->ExceptionAddress);
    else
        wsprintfA(buf, " (0x%08lX)\n", code);
    SafeWrite(buf);

    // Always pass to the next handler (debugger or OS default).
    // Returning EXCEPTION_EXECUTE_HANDLER here swallows the exception
    // before the debugger sees it, which breaks analysis.
    // The __except block in main() will execute regardless because
    // EXCEPTION_CONTINUE_SEARCH from a filter does NOT prevent the
    // __except body from running when using /EHa -- the OS walks the
    // handler chain and will find the __except frame.
    //
    // Correction: EXCEPTION_CONTINUE_SEARCH means "keep searching",
    // so the __except body does NOT run.  To log and then execute the
    // __except body we must return EXCEPTION_EXECUTE_HANDLER, but only
    // for exceptions we know are recoverable (bad_alloc path).
    // STATUS_HEAP_CORRUPTION is NOT recoverable -- let it propagate.
    if (code == STATUS_NO_MEMORY)
        return EXCEPTION_EXECUTE_HANDLER;   // recoverable: bad_alloc loop

    return EXCEPTION_CONTINUE_SEARCH;       // corruption/AV: give to debugger
}