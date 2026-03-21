#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include "ansicolor.h"
#include "runtime.h"
#include "reporter.h"

#pragma comment(lib, "psapi.lib")

namespace
{
    std::atomic<bool>  g_shutdownCalled{ false };
    std::atomic<bool>  g_shuttingDown{ false };

    SRWLOCK            g_srw = SRWLOCK_INIT;
    struct Block { void* ptr; bool isVirtual; };
    std::vector<Block>* g_blocks = nullptr;

    DWORD              g_parentPid      = 0;
    HANDLE             g_watchdogThread = nullptr;
    std::atomic<bool>  g_watchdogStop{ false };


    LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;

    // -------------------------------------------------------------------------
    DWORD GetParentPid()
    {
        DWORD  selfPid = GetCurrentProcessId();
        HANDLE snap    = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W pe{ sizeof(pe) };
        DWORD parent = 0;
        if (Process32FirstW(snap, &pe))
        {
            do {
                if (pe.th32ProcessID == selfPid)
                    { parent = pe.th32ParentProcessID; break; }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return parent;
    }

    // -------------------------------------------------------------------------
    // WatchdogProc: polls parent PID every 100ms.
    // Hidden from debugger via NtSetInformationThread(ThreadHideFromDebugger).
    // -------------------------------------------------------------------------
    DWORD WINAPI WatchdogProc(LPVOID)
    {
        using FnNtSIT = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG);
        auto NtSIT = reinterpret_cast<FnNtSIT>(
            GetProcAddress(GetModuleHandleA("ntdll.dll"),
                           "NtSetInformationThread"));
        if (NtSIT)
            NtSIT(GetCurrentThread(), 0x11 /*ThreadHideFromDebugger*/, nullptr, 0);

        constexpr DWORD kIntervalMs = 100;

        while (!g_watchdogStop.load(std::memory_order_acquire))
        {
            HANDLE hParent = OpenProcess(
                SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE, g_parentPid);

            if (hParent == nullptr)
            {
                if (GetLastError() != ERROR_ACCESS_DENIED)
                {
                    std::fprintf(stderr,
                        AC_WATCHDOG "[Watchdog]" AC_RESET " "
                        AC_FALLSAFE "Parent process gone. Terminating." AC_RESET "\n");
                    std::fflush(stderr);
                    TerminateProcess(GetCurrentProcess(), 0xDEAD0001);
                }
            }
            else
            {
                DWORD wr = WaitForSingleObject(hParent, 0);
                CloseHandle(hParent);
                if (wr == WAIT_OBJECT_0)
                {
                    std::fprintf(stderr,
                        AC_WATCHDOG "[Watchdog]" AC_RESET " "
                        AC_FALLSAFE "Parent exit signal. Terminating." AC_RESET "\n");
                    std::fflush(stderr);
                    TerminateProcess(GetCurrentProcess(), 0xDEAD0002);
                }
            }

            Sleep(kIntervalMs);
        }
        return 0;
    }


    // -------------------------------------------------------------------------
    LONG CALLBACK UnhandledFilter(EXCEPTION_POINTERS* ep)
    {
        std::fprintf(stderr,
            AC_FALLSAFE "[FallSafe] unhandled exception "
            AC_EXCCODE  "0x%08lX"
            AC_FALLSAFE " @ "
            AC_ADDR     "%p"
            AC_FALLSAFE " -- releasing resources" AC_RESET "\n",
            ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress);
        std::fflush(stderr);
        AppRuntime::Shutdown();

        if (g_prevFilter) return g_prevFilter(ep);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void AtExitHandler() { AppRuntime::Shutdown(); }

    // -------------------------------------------------------------------------
    void ReleaseAllBlocks_Locked()
    {
        if (!g_blocks) return;
        size_t freed = 0;
        for (auto& b : *g_blocks)
        {
            if (!b.ptr) continue;
            if (b.isVirtual) VirtualFree(b.ptr, 0, MEM_RELEASE);
            else             std::free(b.ptr);
            b.ptr = nullptr;
            ++freed;
        }
        g_blocks->clear();
        std::printf(
            AC_RUNTIME "[Runtime]" AC_RESET
            " freed " AC_NUM "%zu" AC_RESET " blocks\n", freed);
        std::fflush(stdout);
    }

} // anonymous namespace

// =============================================================================
void AppRuntime::Init(DWORD parentPid)
{
    g_blocks = new std::vector<Block>();
    g_blocks->reserve(4096);

    g_parentPid = (parentPid != 0) ? parentPid : GetParentPid();
    std::printf(
        AC_RUNTIME "[Runtime]" AC_RESET
        " watching parent PID=" AC_NUM "%lu" AC_RESET "\n",
        g_parentPid);
    std::fflush(stdout);


    if (g_parentPid != 0)
    {
        g_watchdogThread = CreateThread(nullptr, 64 * 1024,
                                        WatchdogProc, nullptr, 0, nullptr);
        assert(g_watchdogThread != nullptr);
        SetThreadPriority(g_watchdogThread, THREAD_PRIORITY_ABOVE_NORMAL);
    }

    g_prevFilter = SetUnhandledExceptionFilter(UnhandledFilter);
    std::atexit(AtExitHandler);
}

void AppRuntime::Shutdown()
{
    bool expected = false;
    if (!g_shutdownCalled.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;

    g_shuttingDown.store(true, std::memory_order_release);
    std::printf(AC_RUNTIME "[Runtime]" AC_RESET " shutdown start ...\n");
    std::fflush(stdout);

    if (g_watchdogThread)
    {
        g_watchdogStop.store(true, std::memory_order_release);
        WaitForSingleObject(g_watchdogThread, 1000);
        CloseHandle(g_watchdogThread);
        g_watchdogThread = nullptr;
    }


    AcquireSRWLockExclusive(&g_srw);
    ReleaseAllBlocks_Locked();
    ReleaseSRWLockExclusive(&g_srw);

    delete g_blocks;
    g_blocks = nullptr;

    SetUnhandledExceptionFilter(g_prevFilter);

    Reporter::PrintSystemMemory();
    std::printf(AC_SUCCESS "[Runtime] shutdown complete." AC_RESET "\n");
    std::fflush(stdout);
}

void AppRuntime::RegisterVirtual(void* p)
{
    if (!p || g_shuttingDown.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&g_srw);
    if (g_blocks) g_blocks->push_back({ p, true });
    ReleaseSRWLockExclusive(&g_srw);
}

void AppRuntime::UnregisterVirtual(void* p)
{
    if (!p) return;
    AcquireSRWLockExclusive(&g_srw);
    if (g_blocks)
        for (auto& b : *g_blocks)
            if (b.ptr == p && b.isVirtual) { b.ptr = nullptr; break; }
    ReleaseSRWLockExclusive(&g_srw);
}

void AppRuntime::RegisterHeap(void* p)
{
    if (!p || g_shuttingDown.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&g_srw);
    if (g_blocks) g_blocks->push_back({ p, false });
    ReleaseSRWLockExclusive(&g_srw);
}

void AppRuntime::UnregisterHeap(void* p)
{
    if (!p) return;
    AcquireSRWLockExclusive(&g_srw);
    if (g_blocks)
        for (auto& b : *g_blocks)
            if (b.ptr == p && !b.isVirtual) { b.ptr = nullptr; break; }
    ReleaseSRWLockExclusive(&g_srw);
}

bool AppRuntime::IsShuttingDown() noexcept
{
    return g_shuttingDown.load(std::memory_order_acquire);
}