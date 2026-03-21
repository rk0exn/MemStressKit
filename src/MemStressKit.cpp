// memstresskit.cpp
// Unified build + run tool for MemStresser.
//
// Usage:
//   memstresskit build [debug|release]
//   memstresskit run   [debug|release] [windbg]
//   memstresskit /?  |  -?  |  (no args)   -> help
//
// Build: cl /nologo /EHsc /W4 /std:c++20 /utf-8 /MT
//           memstresskit.cpp
//           /link /SUBSYSTEM:CONSOLE /OUT:memstresskit.exe

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include "ansicolor.h"
#include <psapi.h>
#include <lmcons.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string SelfDir()
{
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    char* p = strrchr(buf, '\\');
    if (p) *(p + 1) = '\0';
    return buf;
}

static void ShowHelp();                  // forward declaration
static int  CmdBuild(const char* mode);  // forward declaration
static void PrintClNotFound();           // forward declaration

// Run an exe directly (bypasses cmd.exe to avoid special-char interpretation).
// suppressStdout=true redirects the child's stdout to NUL so compiler banner
// and source filenames don't interleave with our own output.
// stderr is always inherited so error messages reach the console.
static int RunDirect(const std::string& cmdLine, bool suppressStdout = false)
{
    std::string buf = cmdLine;

    STARTUPINFOA si{ sizeof(si) };
    HANDLE hNul = INVALID_HANDLE_VALUE;

    if (suppressStdout)
    {
        // Open NUL for writing; mark it inheritable.
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        hNul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
                           &sa, OPEN_EXISTING, 0, nullptr);
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = hNul;
        si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    }

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, &buf[0], nullptr, nullptr,
                             TRUE, 0, nullptr, nullptr, &si, &pi);

    if (hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);

    if (!ok)
    {
        DWORD gle = GetLastError();
        if (gle == ERROR_FILE_NOT_FOUND || gle == ERROR_BAD_EXE_FORMAT)
            PrintClNotFound();
        else
        {
            std::fprintf(stderr,
                AC_OOM "[ERROR]" AC_RESET " CreateProcess failed: GLE=%lu\n  cmd: %s\n",
                gle, buf.c_str());
            std::fflush(stderr);
        }
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

// Run a simple shell command via cmd.exe /C (for del, mkdir, etc.).
static void RunShell(const std::string& cmd)
{
    std::string full = "cmd.exe /C " + cmd;
    STARTUPINFOA si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, &full[0], nullptr, nullptr,
                        TRUE, 0, nullptr, nullptr, &si, &pi)) return;
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

// Returns the full path to cl.exe if found on PATH, or empty string.
static std::string FindClExe()
{
    char buf[MAX_PATH] = {};
    if (SearchPathA(nullptr, "cl.exe", nullptr, MAX_PATH, buf, nullptr))
        return buf;
    return {};
}

// Returns the full path to rc.exe if found on PATH, or empty string.
static std::string FindRcExe()
{
    char buf[MAX_PATH] = {};
    if (SearchPathA(nullptr, "rc.exe", nullptr, MAX_PATH, buf, nullptr))
        return buf;
    return {};
}

static void PrintClNotFound()
{
    std::fprintf(stderr,
        AC_OOM "[ERROR]" AC_RESET " cl.exe not found on PATH.\n"
        AC_WARN "        Run this command from:\n" AC_RESET
        "        \"x64 Native Tools Command Prompt for VS 20xx\"\n"
        "        (Start menu -> Visual Studio 20xx -> x64 Native Tools Command Prompt)\n"
        "        Then re-run: memstresskit build <mode>\n");
    std::fflush(stderr);
}

static bool StartDetached(const std::string& exe, const std::string& args)
{
    std::string cmd = "\"" + exe + "\"";
    if (!args.empty()) cmd += " " + args;

    STARTUPINFOA si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, &cmd[0], nullptr, nullptr,
                        FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi))
    {
        std::fprintf(stderr,
            AC_OOM "[ERROR]" AC_RESET " Failed to start: %s  GLE=%lu\n",
            cmd.c_str(), GetLastError());
        std::fflush(stderr);
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

static bool DebuggerAttached()
{
    if (IsDebuggerPresent()) return true;
    BOOL remote = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
    return remote == TRUE;
}

// ---------------------------------------------------------------------------
// CmdInfo
// ---------------------------------------------------------------------------
static void PrintRow(const char* label, const char* val)
{
    std::printf("  " AC_SYSKEY "%-28s" AC_RESET " " AC_SYSVAL "%s" AC_RESET "\n",
                label, val);
}

static void PrintRowU64(const char* label, unsigned long long v, const char* unit)
{
    char buf[64];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%llu %s", v, unit);
    PrintRow(label, buf);
}

static void PrintRowU32(const char* label, unsigned long v, const char* unit)
{
    char buf[64];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%lu %s", v, unit);
    PrintRow(label, buf);
}

static const char* MemHealthStr(DWORD pct)
{
    if (pct >= 90) return AC_OOM      "Critical"  AC_RESET;
    if (pct >= 75) return AC_MEM_HIGH "High"       AC_RESET;
    if (pct >= 50) return AC_MEM_MID  "Moderate"   AC_RESET;
    return                AC_MEM_LOW  "Good"        AC_RESET;
}

static int CmdInfo()
{
    std::printf("\n" AC_HEADER "=== System Information ===" AC_RESET "\n\n");
    std::fflush(stdout);

    // ---- Execution Environment ----------------------------------------
    std::printf(AC_PHASE "[ Execution Environment ]\n" AC_RESET);

    char uname[256] = {};
    DWORD ulen = sizeof(uname);
    GetUserNameA(uname, &ulen);
    PrintRow("User", uname);

    BOOL isAdmin = FALSE;
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
    {
        TOKEN_ELEVATION elev{};
        DWORD sz = sizeof(elev);
        GetTokenInformation(tok, TokenElevation, &elev, sz, &sz);
        isAdmin = elev.TokenIsElevated;
        CloseHandle(tok);
    }
    PrintRow("Administrator",
             isAdmin ? AC_SUCCESS "Yes (elevated)" AC_RESET
                     : AC_WARN    "No"             AC_RESET);

    char pidbuf[32];
    _snprintf_s(pidbuf, sizeof(pidbuf), _TRUNCATE, "%lu", GetCurrentProcessId());
    PrintRow("PID", pidbuf);
    PrintRow("Bitness", sizeof(void*) == 8 ? "x64 (64-bit)" : "x86 (32-bit)");

    // ---- Kernel / OS -------------------------------------------------
    std::printf("\n" AC_PHASE "[ Kernel / OS ]\n" AC_RESET);

    OSVERSIONINFOEXW osvi{ sizeof(osvi) };
    #pragma warning(suppress: 4996)
    GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&osvi));

    char verbuf[64];
    _snprintf_s(verbuf, sizeof(verbuf), _TRUNCATE,
        "%lu.%lu  Build %lu  SP%u.%u",
        osvi.dwMajorVersion, osvi.dwMinorVersion,
        osvi.dwBuildNumber,
        osvi.wServicePackMajor, osvi.wServicePackMinor);
    PrintRow("Windows Version", verbuf);

    const char* prodType =
        (osvi.wProductType == VER_NT_WORKSTATION)       ? "Workstation" :
        (osvi.wProductType == VER_NT_SERVER)            ? "Server" :
        (osvi.wProductType == VER_NT_DOMAIN_CONTROLLER) ? "DC" : "Unknown";
    PrintRow("Product Type", prodType);

    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    const char* arch =
        (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? "x64 (AMD64)" :
        (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) ? "ARM64" : "Other";
    PrintRow("CPU Architecture", arch);
    PrintRowU32("Logical Processors", si.dwNumberOfProcessors, "");
    PrintRowU32("Page Size",          si.dwPageSize,           "B");
    PrintRowU32("Alloc Granularity",  si.dwAllocationGranularity >> 10, "KB");

    // ---- Physical Memory --------------------------------------------
    std::printf("\n" AC_PHASE "[ Physical Memory ]\n" AC_RESET);

    MEMORYSTATUSEX ms{ sizeof(ms) };
    GlobalMemoryStatusEx(&ms);

    PrintRowU64("Total Physical",     ms.ullTotalPhys  >> 20, "MB");
    PrintRowU64("Available Physical", ms.ullAvailPhys  >> 20, "MB");
    PrintRowU64("Used Physical",
        (ms.ullTotalPhys - ms.ullAvailPhys) >> 20, "MB");

    DWORD loadPct = ms.dwMemoryLoad;
    char loadbuf[64];
    _snprintf_s(loadbuf, sizeof(loadbuf), _TRUNCATE, "%lu%%", loadPct);
    PrintRow("Memory Load",   loadbuf);
    PrintRow("Memory Health", MemHealthStr(loadPct));

    // ---- Virtual Memory ---------------------------------------------
    std::printf("\n" AC_PHASE "[ Virtual Memory ]\n" AC_RESET);

    PrintRowU64("Total Virtual",     ms.ullTotalVirtual >> 20, "MB");
    PrintRowU64("Available Virtual", ms.ullAvailVirtual >> 20, "MB");
    PrintRowU64("Used Virtual",
        (ms.ullTotalVirtual - ms.ullAvailVirtual) >> 20, "MB");

    PROCESS_MEMORY_COUNTERS_EX pmc{ sizeof(pmc) };
    GetProcessMemoryInfo(GetCurrentProcess(),
                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                         sizeof(pmc));
    PrintRowU64("Proc WorkingSet",  pmc.WorkingSetSize     >> 20, "MB");
    PrintRowU64("Proc Private",     pmc.PrivateUsage       >> 20, "MB");
    PrintRowU64("Proc PeakWS",      pmc.PeakWorkingSetSize >> 20, "MB");

    // ---- Swap / Paging ----------------------------------------------
    std::printf("\n" AC_PHASE "[ Swap / Paging ]\n" AC_RESET);

    PrintRowU64("Total Page File",    ms.ullTotalPageFile >> 20, "MB");
    PrintRowU64("Available Page File",ms.ullAvailPageFile >> 20, "MB");
    PrintRowU64("Used Page File",
        (ms.ullTotalPageFile - ms.ullAvailPageFile) >> 20, "MB");

    DWORD pfPct = (ms.ullTotalPageFile > 0)
        ? static_cast<DWORD>(
            (ms.ullTotalPageFile - ms.ullAvailPageFile) * 100
            / ms.ullTotalPageFile)
        : 0;
    char pfbuf[32];
    _snprintf_s(pfbuf, sizeof(pfbuf), _TRUNCATE, "%lu%%", pfPct);
    PrintRow("Page File Load",  pfbuf);
    PrintRow("Page File Health",MemHealthStr(pfPct));
    PrintRowU32("Page Fault Count", pmc.PageFaultCount, "");

    std::printf("\n");
    std::fflush(stdout);
    return 0;
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------
static void ShowHelp()
{
    std::printf(
        "\n"
        AC_HEADER "MemStressKit" AC_RESET
        " -- build and run tool for MemStresser\n"
        "\n"
        AC_SYSKEY "Usage:" AC_RESET "\n"
        "  memstresskit " AC_ITEM_NUM "build" AC_RESET " " AC_ITEM_TEXT "<mode>" AC_RESET "\n"
        "  memstresskit " AC_ITEM_NUM "run  " AC_RESET " " AC_ITEM_TEXT "<mode>" AC_RESET " [windbg]\n"
        "  memstresskit " AC_ITEM_NUM "info " AC_RESET "\n"
        "  memstresskit " AC_ITEM_NUM "help " AC_RESET "  |  "
                         AC_ITEM_NUM "/h" AC_RESET "  |  "
                         AC_ITEM_NUM "-h" AC_RESET "  |  "
                         AC_ITEM_NUM "--help" AC_RESET "  |  "
                         AC_ITEM_NUM "/?" AC_RESET "  |  "
                         AC_ITEM_NUM "-?" AC_RESET "\n"
        "\n"
        AC_SYSKEY "  mode:" AC_RESET "\n"
        "    " AC_ITEM_NUM "debug  " AC_RESET "  Debug build / run   (VCRT DLL, PDB)  -> " AC_NUM "Debug\\\n" AC_RESET
        "    " AC_ITEM_NUM "release" AC_RESET "  Release build / run (/MT,  no PDB)   -> " AC_NUM "Release\\\n" AC_RESET
        "    " AC_ITEM_NUM "all    " AC_RESET "  Build both debug and release\n"
        "\n"
        AC_SYSKEY "  run options:" AC_RESET "\n"
        "    " AC_ITEM_NUM "windbg" AC_RESET "   Launch under WinDbg instead of DummyDebugger\n"
        "\n"
        AC_SYSKEY "Examples:" AC_RESET "\n"
        "  memstresskit build debug\n"
        "  memstresskit build release\n"
        "  memstresskit build all\n"
        "  memstresskit run debug\n"
        "  memstresskit run release\n"
        "  memstresskit run debug windbg\n"
        "  memstresskit info\n"
        "  memstresskit help\n"
        "\n"
    );
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------
static int CmdBuild(const char* mode)
{
    // 'all' builds both modes sequentially
    if (_stricmp(mode, "all") == 0)
    {
        int r = CmdBuild("debug");
        if (r != 0) return r;
        return CmdBuild("release");
    }

    const bool isRelease = (_stricmp(mode, "release") == 0);
    const bool isDebug   = (_stricmp(mode, "debug")   == 0);
    if (!isRelease && !isDebug)
    {
        std::fprintf(stderr,
            AC_OOM "[ERROR]" AC_RESET " Unknown mode: %s  (use debug, release, or all)\n", mode);
        std::fflush(stderr);
        ShowHelp();
        return 1;
    }

    if (FindClExe().empty())
    {
        PrintClNotFound();
        return 1;
    }

    std::string dir    = SelfDir();
    std::string srcDir = dir + "src\\";
    std::string outDir = dir + (isRelease ? "Release" : "Debug");

    CreateDirectoryA(outDir.c_str(), nullptr);

    std::string clFlags = isRelease
        ? "/nologo /EHa /O2 /W4 /std:c++20 /arch:AVX512 /MT /utf-8"
        : "/nologo /EHa /Zi /Od /W4 /std:c++20 /arch:AVX512 /MDd /utf-8";
    std::string manifestFlag =
        "/MANIFEST:EMBED /MANIFESTINPUT:\"" + srcDir + "app.manifest\"";
    std::string linkFlags = isRelease
        ? "/MACHINE:X64 /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF"
        : "/MACHINE:X64 /DEBUG /SUBSYSTEM:CONSOLE";
    linkFlags += " " + manifestFlag;
    std::string pdbMain  = isRelease ? "" : ("/PDB:\"" + outDir + "\\MemStresser.pdb\"");
    std::string pdbDummy = isRelease ? "" : ("/PDB:\"" + outDir + "\\DummyDebugger.pdb\"");
    std::string foMain   = "/Fo\"" + outDir + "\\\\\"";
    std::string outMain  = "/OUT:\"" + outDir + "\\MemStresser.exe\"";
    std::string outDummy = "/OUT:\"" + outDir + "\\DummyDebugger.exe\"";

    std::string srcMain  = "\"" + srcDir + "main.cpp\" "
                         + "\"" + srcDir + "stress.cpp\" "
                         + "\"" + srcDir + "simd.cpp\" "
                         + "\"" + srcDir + "reporter.cpp\" "
                         + "\"" + srcDir + "runtime.cpp\"";
    std::string srcDummy = "\"" + srcDir + "DummyDebugger.c\"";

    std::printf(AC_RUNTIME "[Build]" AC_RESET
        "  mode=" AC_NUM "%s" AC_RESET
        "  out=" AC_ADDR "%s" AC_RESET "\n",
        mode, outDir.c_str());
    std::fflush(stdout);

    // --- rc.exe detection ---
    std::string rcExe = FindRcExe();
    if (rcExe.empty())
    {
        std::fprintf(stderr,
            AC_WARN "[WARN]" AC_RESET
            " rc.exe not found on PATH -- icons will not be embedded.\n");
        std::fflush(stderr);
    }

    // Helper: write a one-line .rc, compile to .res, return res path.
    // Returns empty string if rc.exe is unavailable or compilation fails.
    auto CompileRc = [&](const char* icoName, const char* resName) -> std::string
    {
        if (rcExe.empty()) return {};
        std::string icoPath = srcDir + icoName;
        // Skip silently if the .ico file is absent
        if (GetFileAttributesA(icoPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            std::fprintf(stderr,
                AC_WARN "[WARN]" AC_RESET " Icon not found, skipping: %s\n",
                icoPath.c_str());
            std::fflush(stderr);
            return {};
        }
        std::string rcPath  = outDir + "\\" + resName + ".rc";
        std::string resPath = outDir + "\\" + resName + ".res";

        // Write minimal .rc: "1 ICON <path>"
        // Use a FILE* so we never touch the CRT heap error path.
        FILE* f = nullptr;
        if (fopen_s(&f, rcPath.c_str(), "w") != 0 || !f)
        {
            std::fprintf(stderr,
                AC_OOM "[ERROR]" AC_RESET " Cannot write %s\n", rcPath.c_str());
            std::fflush(stderr);
            return {};
        }
        std::fprintf(f, "1 ICON \"%s\"\r\n", icoPath.c_str());
        std::fclose(f);

        // rc.exe /nologo /fo <res> <rc>
        std::string cmd = "\"" + rcExe + "\""
                        + " /nologo"
                        + " /fo\"" + resPath + "\""
                        + " \"" + rcPath + "\"";
        if (RunDirect(cmd, true) != 0)
        {
            std::fprintf(stderr,
                AC_OOM "[FAILED]" AC_RESET " rc.exe: %s\n", rcPath.c_str());
            std::fflush(stderr);
            DeleteFileA(rcPath.c_str());
            return {};
        }
        DeleteFileA(rcPath.c_str());   // .rc は不要なので即削除
        return resPath;
    };

    // --- MemStresser.exe ---
    std::printf(AC_RUNTIME "[Build]" AC_RESET " Compiling MemStresser.exe ...\n");
    std::fflush(stdout);
    std::string resMain = CompileRc("MemStresser.ico", "MemStresser");
    std::string cmdMain = "cl " + clFlags + " " + srcMain + " " + foMain
                        + " /link " + linkFlags
                        + " psapi.lib advapi32.lib user32.lib "
                        + (resMain.empty() ? "" : ("\"" + resMain + "\" "))
                        + pdbMain + " " + outMain;
    if (RunDirect(cmdMain, true) != 0)
    {
        std::fprintf(stderr, AC_OOM "[FAILED]" AC_RESET " MemStresser.exe\n");
        std::fflush(stderr);
        if (!resMain.empty()) DeleteFileA(resMain.c_str());
        return 1;
    }
    if (!resMain.empty()) DeleteFileA(resMain.c_str());

    // --- DummyDebugger.exe ---
    std::printf(AC_RUNTIME "[Build]" AC_RESET " Compiling DummyDebugger.exe ...\n");
    std::fflush(stdout);
    std::string resDummy = CompileRc("DummyDebugger.ico", "DummyDebugger");
    std::string foD  = "/Fo\"" + outDir + "\\\\\"";
    std::string cmdD = "cl /nologo /W3 /O1 /utf-8 /MT " + srcDummy + " " + foD
                     + " /link /MACHINE:X64 /SUBSYSTEM:WINDOWS user32.lib "
                     + manifestFlag + " "
                     + (resDummy.empty() ? "" : ("\"" + resDummy + "\" "))
                     + pdbDummy + " " + outDummy;
    if (RunDirect(cmdD, true) != 0)
    {
        std::fprintf(stderr, AC_OOM "[FAILED]" AC_RESET " DummyDebugger.exe\n");
        std::fflush(stderr);
        if (!resDummy.empty()) DeleteFileA(resDummy.c_str());
        return 1;
    }
    if (!resDummy.empty()) DeleteFileA(resDummy.c_str());

    RunShell("del /q \"" + outDir + "\\*.obj\" 2>nul");



    std::printf("\n"
        AC_SUCCESS "[OK]" AC_RESET " %s\\MemStresser.exe\n"
        AC_SUCCESS "[OK]" AC_RESET " %s\\DummyDebugger.exe\n\n",
        outDir.c_str(), outDir.c_str());
    std::fflush(stdout);
    return 0;
}

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------
static int CmdRun(const char* mode, bool useWinDbg)
{
    const bool isRelease = (_stricmp(mode, "release") == 0);
    const bool isDebug   = (_stricmp(mode, "debug")   == 0);
    if (!isRelease && !isDebug)
    {
        std::fprintf(stderr,
            AC_OOM "[ERROR]" AC_RESET " Unknown mode: %s  (use debug or release)\n", mode);
        std::fflush(stderr);
        ShowHelp();
        return 1;
    }

    std::string dir      = SelfDir();
    std::string outDir   = dir + (isRelease ? "Release" : "Debug");
    std::string exeMain  = outDir + "\\MemStresser.exe";
    std::string exeDummy = outDir + "\\DummyDebugger.exe";

    if (GetFileAttributesA(exeMain.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        std::fprintf(stderr,
            AC_OOM "[ERROR]" AC_RESET " Not found: %s\n"
            "        Run: memstresskit build %s\n",
            exeMain.c_str(), mode);
        std::fflush(stderr);
        return 1;
    }

    if (!useWinDbg &&
        GetFileAttributesA(exeDummy.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        std::printf(AC_WARN "[INFO]" AC_RESET
            " DummyDebugger.exe not found, building...\n");
        std::fflush(stdout);
        if (CmdBuild(mode) != 0) return 1;
    }

    // --- WinDbg mode ---
    if (useWinDbg)
    {
        const char* paths[] = {
            "C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\windbg.exe",
            "C:\\Program Files\\Windows Kits\\10\\Debuggers\\x64\\windbg.exe",
            "C:\\Debuggers\\x64\\windbg.exe",
            nullptr
        };
        std::string windbg;
        for (int i = 0; paths[i]; ++i)
            if (GetFileAttributesA(paths[i]) != INVALID_FILE_ATTRIBUTES)
                { windbg = paths[i]; break; }

        if (windbg.empty())
        {
            char buf[MAX_PATH] = {};
            if (SearchPathA(nullptr, "windbg.exe", nullptr, MAX_PATH, buf, nullptr))
                windbg = buf;
        }
        if (windbg.empty())
        {
            std::fprintf(stderr, AC_OOM "[ERROR]" AC_RESET " windbg.exe not found.\n");
            std::fflush(stderr);
            return 1;
        }
        std::printf(AC_RUNTIME "[Run]" AC_RESET
            " WinDbg: " AC_ADDR "%s" AC_RESET "\n", windbg.c_str());
        std::fflush(stdout);
        return StartDetached(windbg, "-g -o \"" + exeMain + "\"") ? 0 : 1;
    }

    // --- DummyDebugger mode ---
    if (DebuggerAttached())
    {
        std::printf(AC_OK "[Run]" AC_RESET
            " Debugger already attached. Running directly...\n");
        std::fflush(stdout);
        STARTUPINFOA si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        std::string cmd = "\"" + exeMain + "\"";
        if (!CreateProcessA(nullptr, &cmd[0], nullptr, nullptr,
                            TRUE, 0, nullptr, nullptr, &si, &pi))
        {
            std::fprintf(stderr,
                AC_OOM "[ERROR]" AC_RESET " Failed to start MemStresser.exe\n");
            std::fflush(stderr);
            return 1;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return static_cast<int>(code);
    }

    std::printf(AC_RUNTIME "[Run]" AC_RESET
        " Launching under DummyDebugger"
        "  " AC_NUM "%s" AC_RESET "\n", mode);
    std::fflush(stdout);
    return StartDetached(exeDummy, "\"" + exeMain + "\"") ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    Ansi::EnableVT();

    if (argc < 2)                               { ShowHelp(); return 0; }
    if (_stricmp(argv[1], "help"   ) == 0)     { ShowHelp(); return 0; }
    if (strcmp(argv[1],   "/?"     ) == 0)     { ShowHelp(); return 0; }
    if (strcmp(argv[1],   "-?"     ) == 0)     { ShowHelp(); return 0; }
    if (strcmp(argv[1],   "/h"     ) == 0)     { ShowHelp(); return 0; }
    if (strcmp(argv[1],   "-h"     ) == 0)     { ShowHelp(); return 0; }
    if (strcmp(argv[1],   "--help" ) == 0)     { ShowHelp(); return 0; }

    if (_stricmp(argv[1], "build") == 0)
    {
        if (argc < 3 ||
            strcmp(argv[2], "/?"    ) == 0 ||
            strcmp(argv[2], "-?"    ) == 0 ||
            strcmp(argv[2], "/h"    ) == 0 ||
            strcmp(argv[2], "-h"    ) == 0 ||
            strcmp(argv[2], "--help") == 0)  { ShowHelp(); return 0; }
        return CmdBuild(argv[2]);
    }

    if (_stricmp(argv[1], "info") == 0)
        return CmdInfo();

    if (_stricmp(argv[1], "run") == 0)
    {
        if (argc < 3 ||
            strcmp(argv[2], "/?") == 0 ||
            strcmp(argv[2], "-?") == 0)    { ShowHelp(); return 0; }
        bool windbg = (argc >= 4 && _stricmp(argv[3], "windbg") == 0);
        return CmdRun(argv[2], windbg);
    }

    std::fprintf(stderr,
        AC_OOM "[ERROR]" AC_RESET " Unknown command: %s\n", argv[1]);
    std::fflush(stderr);
    ShowHelp();
    return 1;
}