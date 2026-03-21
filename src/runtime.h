#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstddef>

// AppRuntime: resource registry, parent watchdog, fallsafe shutdown.
//
// Thread safety:
//   RegisterBlock / UnregisterBlock are protected by SRWLOCK (exclusive).
//   Watchdog is stopped via an atomic flag.
namespace AppRuntime
{
    // Call once at program start.
    // parentPid: PID to watch (0 = auto-detect from process snapshot).
    void Init(DWORD parentPid = 0);

    // Idempotent shutdown: release all resources, stop threads.
    void Shutdown();

    // VirtualAlloc block registry (freed with VirtualFree on Shutdown).
    void RegisterVirtual(void* p);
    void UnregisterVirtual(void* p);

    // malloc/new block registry (freed with free() on Shutdown).
    void RegisterHeap(void* p);
    void UnregisterHeap(void* p);

    bool IsShuttingDown() noexcept;
}