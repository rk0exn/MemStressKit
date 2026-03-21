#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <new>
#include <vector>
#include <atomic>
#include <thread>
#include <stop_token>
#include <cstdio>
#include <cstdlib>
#include "ansicolor.h"
#include "simd.h"
#include "stress.h"
#include "reporter.h"
#include "runtime.h"

#pragma comment(lib, "advapi32.lib")

// ---- 1. Single large block (VirtualAlloc) ----------------------------------
void Stress::SingleLargeBlock()
{
    std::printf(AC_SCENARIO "[1]" AC_RESET " SingleLargeBlock: VirtualAlloc stress"
                "  [SIMD=" AC_NUM "%s" AC_RESET "]\n", Simd::LevelName());
    std::fflush(stdout);

    SIZE_T size = 256ULL * 1024 * 1024;
    void*  last = nullptr;

    while (size >= 4096)
    {
        if (AppRuntime::IsShuttingDown()) break;

        void* p = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p)
        {
            AppRuntime::RegisterVirtual(p);
            std::printf(
                "  " AC_OK "OK" AC_RESET
                "  " AC_NUM "%zu" AC_SYSKEY "MB" AC_RESET
                "  @ " AC_ADDR "%p" AC_RESET "\n",
                size >> 20, p);
            std::fflush(stdout);

            Simd::Fill32(p, 0xDEADBEEF, size / sizeof(uint32_t));

            AppRuntime::UnregisterVirtual(p);
            VirtualFree(p, 0, MEM_RELEASE);
            last = p;
            size *= 2;
        }
        else
        {
            std::printf(
                "  " AC_OOM "[OOM]" AC_RESET
                "  " AC_NUM "%zu" AC_SYSKEY "MB" AC_RESET
                " failed  GLE=" AC_EXCCODE "%lu" AC_RESET "\n",
                size >> 20, GetLastError());
            std::fflush(stdout);
            Reporter::PrintSystemMemory();
            break;
        }
    }
    (void)last;
}

// ---- 2. Heap fragmentation stress ------------------------------------------
void Stress::FragmentHeap()
{
    std::printf(AC_SCENARIO "[2]" AC_RESET " FragmentHeap: heap fragmentation stress\n");
    std::fflush(stdout);

    constexpr size_t kCount = 1 << 20;
    std::vector<void*> ptrs(kCount, nullptr);

    std::printf("  " AC_PHASE "Phase 1" AC_RESET ": alloc 32-256B x %zu\n", kCount);
    std::fflush(stdout);
    for (size_t i = 0; i < kCount; ++i)
    {
        ptrs[i] = std::malloc(32 + (i % 225));
        if (!ptrs[i])
        {
            std::printf(
                "  " AC_OOM "[OOM]" AC_RESET
                " malloc failed at i=" AC_NUM "%zu" AC_RESET "\n", i);
            std::fflush(stdout);
            break;
        }
    }

    std::printf("  " AC_PHASE "Phase 2" AC_RESET ": free odd indices\n");
    std::fflush(stdout);
    for (size_t i = 1; i < kCount; i += 2)
        { std::free(ptrs[i]); ptrs[i] = nullptr; }

    std::printf("  " AC_PHASE "Phase 3" AC_RESET ": re-alloc 512B into gaps\n");
    std::fflush(stdout);
    size_t failed = 0;
    for (size_t i = 1; i < kCount; i += 2)
    {
        ptrs[i] = std::malloc(512);
        if (!ptrs[i]) ++failed;
    }
    std::printf(
        "  Re-alloc failures: " AC_OOM "%zu" AC_RESET " / " AC_NUM "%zu" AC_RESET "\n",
        failed, kCount / 2);
    std::fflush(stdout);

    for (auto p : ptrs) std::free(p);
}

// ---- 3. STL container explosion --------------------------------------------
void Stress::STLContainerExplosion()
{
    std::printf(AC_SCENARIO "[3]" AC_RESET " STLContainerExplosion: vector<vector> bomb\n");
    std::fflush(stdout);

    std::vector<std::vector<char>> outer;
    outer.reserve(1024);

    try
    {
        while (true)
        {
            outer.emplace_back(1ULL << 22);
            if ((outer.size() & 0xFF) == 0)
            {
                std::printf(
                    "  chunks=" AC_NUM "%zu" AC_RESET
                    "  total=" AC_NUM "%zu" AC_SYSKEY "MB" AC_RESET "\n",
                    outer.size(), outer.size() * 4);
                std::fflush(stdout);
            }
        }
    }
    catch (const std::bad_alloc& e)
    {
        std::printf(
            "  " AC_OOM "[OOM] bad_alloc" AC_RESET
            ": %s  chunks=" AC_NUM "%zu" AC_RESET "\n",
            e.what(), outer.size());
        std::fflush(stdout);
    }
}

// ---- 4. Heap exhaust + bad_alloc -------------------------------------------
void Stress::HeapExhaustBadAlloc()
{
    std::printf(AC_SCENARIO "[4]" AC_RESET " HeapExhaustBadAlloc: new[] loop\n");
    std::fflush(stdout);

    constexpr size_t kChunk = 1ULL << 26;
    std::vector<char*> blocks;

    try
    {
        for (;;)
        {
            if (AppRuntime::IsShuttingDown()) break;
            char* p = new char[kChunk];
            p[0] = p[kChunk - 1] = '\xCC';
            AppRuntime::RegisterHeap(p);
            blocks.push_back(p);
            std::printf(
                "  allocated: " AC_NUM "%zu" AC_SYSKEY "GB" AC_RESET "\n",
                (blocks.size() * kChunk) >> 30);
            std::fflush(stdout);
        }
    }
    catch (const std::bad_alloc&)
    {
        std::printf(
            "  " AC_OOM "[OOM] bad_alloc" AC_RESET
            "  blocks=" AC_NUM "%zu" AC_RESET
            "  total=" AC_NUM "%zu" AC_SYSKEY "GB" AC_RESET "\n",
            blocks.size(), (blocks.size() * kChunk) >> 30);
        std::fflush(stdout);
    }

    for (auto p : blocks)
    {
        AppRuntime::UnregisterHeap(p);
        delete[] p;
    }
}

// ---- 5. Multi-threaded heap race (C++20 std::jthread) ----------------------
void Stress::MultiThreadedRace()
{
    std::printf(AC_SCENARIO "[5]" AC_RESET " MultiThreadedRace: 8 threads x 5s\n");
    std::fflush(stdout);

    constexpr int kThreads = 8;
    std::atomic<bool>   stop{ false };
    std::atomic<size_t> oomCount{ 0 };

    auto worker = [&](std::stop_token stoken, DWORD tid)
    {
        std::vector<void*> local;
        local.reserve(4096);

        while (!stop.load(std::memory_order_acquire) && !stoken.stop_requested())
        {
            if (AppRuntime::IsShuttingDown()) break;

            size_t sz = 1024 + (tid & 0xFF) + (local.size() & 0x3F);
            void* p = std::malloc(sz);
            if (p)
                local.push_back(p);
            else
            {
                oomCount.fetch_add(1, std::memory_order_relaxed);
                for (auto x : local) std::free(x);
                local.clear();
            }
            if (local.size() > 1024)
            {
                for (size_t i = 0; i < local.size(); i += 2)
                    { std::free(local[i]); local[i] = nullptr; }
                local.erase(
                    std::remove(local.begin(), local.end(), nullptr),
                    local.end());
            }
        }
        for (auto x : local) std::free(x);
    };

    std::vector<std::jthread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        DWORD tid = GetCurrentThreadId() + static_cast<DWORD>(i);
        threads.emplace_back(worker, tid);
        std::printf("  " AC_RUNTIME "Thread #%d started" AC_RESET "\n", i + 1);
        std::fflush(stdout);
    }

    Sleep(5000);
    stop.store(true, std::memory_order_release);
    // jthread destructor calls request_stop() + join() automatically

    const size_t oom = oomCount.load();
    std::printf(
        "  OOM count: %s" AC_BOLD "%zu" AC_RESET "\n",
        oom > 0 ? AC_OOM : AC_OK, oom);
    std::fflush(stdout);
}

// ---- 6. AWE / Large Pages --------------------------------------------------
void Stress::AWELargePages()
{
    std::printf(AC_SCENARIO "[6]" AC_RESET " AWELargePages: large page limit"
                "  [SIMD=" AC_NUM "%s" AC_RESET "]\n", Simd::LevelName());
    std::fflush(stdout);

    HANDLE token = nullptr;
    OpenProcessToken(GetCurrentProcess(),
                     TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token);

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    LookupPrivilegeValueA(nullptr, SE_LOCK_MEMORY_NAME, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
    bool hasPriv = (GetLastError() == ERROR_SUCCESS);
    if (token) CloseHandle(token);

    SIZE_T largePageSz = GetLargePageMinimum();
    std::printf(
        "  LargePageMin=" AC_NUM "%zu" AC_SYSKEY "KB" AC_RESET
        "  priv=%s\n",
        largePageSz >> 10,
        hasPriv ? AC_OK "yes" : AC_WARN "no");
    std::fflush(stdout);

    SIZE_T target = largePageSz * 512;
    DWORD  flags  = MEM_COMMIT | MEM_RESERVE;
    if (hasPriv) flags |= MEM_LARGE_PAGES;

    void* p = VirtualAlloc(nullptr, target, flags, PAGE_READWRITE);
    if (p)
    {
        AppRuntime::RegisterVirtual(p);
        std::printf(
            "  " AC_OK "OK" AC_RESET
            "  " AC_NUM "%zu" AC_SYSKEY "MB" AC_RESET
            "  @ " AC_ADDR "%p" AC_RESET "\n",
            target >> 20, p);
        std::fflush(stdout);

        Simd::Fill32(p, 0xC0FFEE00, target / sizeof(uint32_t));

        AppRuntime::UnregisterVirtual(p);
        VirtualFree(p, 0, MEM_RELEASE);
    }
    else
    {
        std::printf(
            "  " AC_OOM "[FAIL]" AC_RESET
            "  GLE=" AC_EXCCODE "%lu" AC_RESET
            "  LargePages=%s\n",
            GetLastError(), hasPriv ? "enabled" : "disabled");
        std::fflush(stdout);
    }
}