// MemStresser v2 -- OOM debug tool (C++20 / SIMD)
//
// Build via: memstresskit build debug|release

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <new>
#include <cstdio>
#include <cstdlib>
#include "ansicolor.h"
#include "simd.h"
#include "stress.h"
#include "reporter.h"
#include "runtime.h"

// ---------------------------------------------------------------------------
// SIMD compatibility check
//
// Compares the /arch flag baked in at compile time against the CPU's
// runtime capability detected by Simd::Detect().
//
// Required runtime level per build:
//   /arch:AVX512  (AVX512 / AVX10.2 builds) -> Simd::Level::AVX512 or higher
//   /arch:AVX2    (AVX2 build)               -> Simd::Level::AVX2   or higher
//   no /arch      (Fallback build)           -> always OK
//
// Returns true if the build is runnable on this CPU.
// ---------------------------------------------------------------------------
static bool CheckSimdCompat(Simd::Level runtimeLevel)
{
#if defined(__AVX512F__)
    // Built with /arch:AVX512 -- need at least AVX-512F at runtime
    if (runtimeLevel < Simd::Level::AVX512)
    {
        std::fprintf(stderr,
            AC_OOM "\n[!] This binary was built with /arch:AVX512\n"
            "    but this CPU only supports: " AC_WARN "%s" AC_OOM "\n"
            "    Rebuild with a lower SIMD option:\n"
            "      memstresskit build release avx2\n"
            "      memstresskit build release\n"
            AC_RESET "\nPress Enter to exit...",
            Simd::LevelName());
        std::fflush(stderr);
        std::getchar();
        return false;
    }
#elif defined(__AVX2__)
    // Built with /arch:AVX2 -- need at least AVX2 at runtime
    if (runtimeLevel < Simd::Level::AVX2)
    {
        std::fprintf(stderr,
            AC_OOM "\n[!] This binary was built with /arch:AVX2\n"
            "    but this CPU only supports: " AC_WARN "%s" AC_OOM "\n"
            "    Rebuild with the fallback option:\n"
            "      memstresskit build release\n"
            AC_RESET "\nPress Enter to exit...",
            Simd::LevelName());
        std::fflush(stderr);
        std::getchar();
        return false;
    }
#endif
    return true;
}

static bool RequireDebugger()
{
    if (IsDebuggerPresent()) return true;
    BOOL remote = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote) && remote)
        return true;
    return false;
}

static void NewHandler()
{
    Reporter::OnNewHandlerFired();
    throw std::bad_alloc{};
}

static LONG CALLBACK VEH(EXCEPTION_POINTERS* ep)
{
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == STATUS_NO_MEMORY || code == 0xC0000374UL)
        Reporter::OnVEH(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void PrintMenu()
{
    std::printf(
        "\n"
        AC_TITLE  "  +===================================+\n"
                  "  |       MemStresser  v2             |\n"
                  "  +===================================+" AC_RESET "\n"
        "  " AC_ITEM_NUM " 1" AC_RESET "  " AC_ITEM_TEXT "SingleLargeBlock   " AC_RESET " VirtualAlloc\n"
        "  " AC_ITEM_NUM " 2" AC_RESET "  " AC_ITEM_TEXT "FragmentHeap       " AC_RESET " malloc loop\n"
        "  " AC_ITEM_NUM " 3" AC_RESET "  " AC_ITEM_TEXT "STLContainerBomb   " AC_RESET " vector<vector>\n"
        "  " AC_ITEM_NUM " 4" AC_RESET "  " AC_ITEM_TEXT "HeapExhaustBadAlloc" AC_RESET " bad_alloc\n"
        "  " AC_ITEM_NUM " 5" AC_RESET "  " AC_ITEM_TEXT "MultiThreadedRace  " AC_RESET " heap contention\n"
        "  " AC_ITEM_NUM " 6" AC_RESET "  " AC_ITEM_TEXT "AWELargePages      " AC_RESET " large page limit\n"
        "  " AC_ITEM_NUM " 7" AC_RESET "  " AC_ITEM_TEXT "SIMD info          " AC_RESET " show CPU level\n"
        "  " AC_ITEM_NUM " 0" AC_RESET "  " AC_WARN "Exit" AC_RESET "\n"
        "  " AC_PROMPT ">" AC_RESET " "
    );
    std::fflush(stdout);
}

int main()
{
    Ansi::EnableVT();

    // SIMD capability detection (once at startup)
    const Simd::Level simdLevel = Simd::Detect();

    if (!CheckSimdCompat(simdLevel))
        return EXIT_FAILURE;

    if (!RequireDebugger())
    {
        std::fprintf(stderr,
            AC_FALLSAFE "[!] No debugger attached. Exiting." AC_RESET "\n");
        std::fflush(stderr);
        return EXIT_FAILURE;
    }

    AppRuntime::Init();
    Reporter::Init();
    std::set_new_handler(NewHandler);

    // Print SIMD level banner
    std::printf(AC_SYSKEY "SIMD" AC_RESET " : " AC_NUM "%s" AC_RESET "\n",
                Simd::LevelName());
    std::fflush(stdout);

    PVOID veh = AddVectoredExceptionHandler(1, VEH);
    Reporter::PrintSystemMemory();

    int choice = -1;
    while (choice != 0 && !AppRuntime::IsShuttingDown())
    {
        PrintMenu();
        if (::scanf_s("%d", &choice) != 1) break;

        __try
        {
            switch (choice)
            {
            case 1: Stress::SingleLargeBlock();       break;
            case 2: Stress::FragmentHeap();           break;
            case 3: Stress::STLContainerExplosion();  break;
            case 4: Stress::HeapExhaustBadAlloc();    break;
            case 5: Stress::MultiThreadedRace();      break;
            case 6: Stress::AWELargePages();          break;
            case 7:
                std::printf(
                    "  SIMD level : " AC_NUM "%s" AC_RESET "\n"
                    "  Enum value : " AC_NUM "%d" AC_RESET "\n",
                    Simd::LevelName(), static_cast<int>(simdLevel));
                std::fflush(stdout);
                break;
            case 0: break;
            default:
                std::printf(AC_INVALID "  Unknown: %d" AC_RESET "\n", choice);
                std::fflush(stdout);
                break;
            }
        }
        __except (Reporter::SEHFilter(GetExceptionCode(), GetExceptionInformation()))
        {
        }

        Reporter::PrintSystemMemory();
    }

    RemoveVectoredExceptionHandler(veh);
    AppRuntime::Shutdown();
    return EXIT_SUCCESS;
}