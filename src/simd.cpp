// simd.cpp
// CPUID-based SIMD detection and runtime dispatch.
// Compiled with /arch:AVX512 so all intrinsic headers are available.
// AVX10.2 is detected at runtime via CPUID leaf 24H sub-leaf 0.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>         // __cpuid, __cpuidex, __stosd
#include <immintrin.h>      // AVX2 / AVX-512 / AVX10 intrinsics
#include <cstdint>
#include <cstring>
#include "simd.h"

namespace
{
    Simd::Level g_level = Simd::Level::Fallback;

    // ---- CPUID helpers -------------------------------------------------------
    struct CpuId { uint32_t eax, ebx, ecx, edx; };

    static CpuId cpuid(uint32_t leaf, uint32_t subleaf = 0) noexcept
    {
        int r[4];
        __cpuidex(r, static_cast<int>(leaf), static_cast<int>(subleaf));
        return { static_cast<uint32_t>(r[0]), static_cast<uint32_t>(r[1]),
                 static_cast<uint32_t>(r[2]), static_cast<uint32_t>(r[3]) };
    }

    static bool xsave_enabled() noexcept
    {
        // OSXSAVE bit (ECX[27]) must be set for YMM/ZMM state management.
        auto c1 = cpuid(1);
        return (c1.ecx & (1u << 27)) != 0;
    }

    static bool avx2_supported() noexcept
    {
        if (!xsave_enabled()) return false;
        uint64_t xcr0 = _xgetbv(0);
        if ((xcr0 & 0x6) != 0x6) return false;          // YMM state
        auto c7 = cpuid(7, 0);
        return (c7.ebx & (1u << 5)) != 0;               // AVX2
    }

    static bool avx512f_supported() noexcept
    {
        if (!xsave_enabled()) return false;
        uint64_t xcr0 = _xgetbv(0);
        if ((xcr0 & 0xE6) != 0xE6) return false;        // ZMM + opmask + YMM
        auto c7 = cpuid(7, 0);
        return (c7.ebx & (1u << 16)) != 0;              // AVX-512F
    }

    // AVX10.2 detection via CPUID leaf 24H (Intel Architecture Spec 2023+).
    // EBX[7:0] = AVX10 version number (>= 2 means AVX10.2).
    // ECX[18]  = 512-bit vector support.
    static bool avx10_2_supported() noexcept
    {
        if (!avx512f_supported()) return false;
        // Check max leaf
        auto c0 = cpuid(0);
        if (c0.eax < 0x24) return false;
        auto c24 = cpuid(0x24, 0);
        uint32_t ver512 = c24.ebx & 0xFF;               // version
        bool     has512 = (c24.ecx & (1u << 18)) != 0;  // 512-bit capable
        return (ver512 >= 2) && has512;
    }

    // ---- Fill implementations -----------------------------------------------

    // AVX10.2 / AVX-512F: 512-bit stores, 64-byte aligned.
    static void fill32_avx512(void* dst, uint32_t val, size_t count) noexcept
    {
        __m512i v = _mm512_set1_epi32(static_cast<int>(val));
        auto*   p = static_cast<__m512i*>(dst);
        size_t  n = count / 16;                          // 16 dwords per ZMM
        for (size_t i = 0; i < n; ++i)
            _mm512_store_si512(p + i, v);
        // tail
        size_t done = n * 16;
        if (done < count)
            __stosd(reinterpret_cast<DWORD*>(p + n), val,
                    static_cast<DWORD>(count - done));
    }

    // AVX2: 256-bit stores, 32-byte aligned.
    static void fill32_avx2(void* dst, uint32_t val, size_t count) noexcept
    {
        __m256i v = _mm256_set1_epi32(static_cast<int>(val));
        auto*   p = static_cast<__m256i*>(dst);
        size_t  n = count / 8;                           // 8 dwords per YMM
        for (size_t i = 0; i < n; ++i)
            _mm256_store_si256(p + i, v);
        size_t done = n * 8;
        if (done < count)
            __stosd(reinterpret_cast<DWORD*>(p + n), val,
                    static_cast<DWORD>(count - done));
    }

} // anonymous namespace

namespace Simd
{
    Level Detect() noexcept
    {
        if      (avx10_2_supported()) g_level = Level::AVX10_2;
        else if (avx512f_supported()) g_level = Level::AVX512;
        else if (avx2_supported())    g_level = Level::AVX2;
        else                          g_level = Level::Fallback;
        return g_level;
    }

    Level Current() noexcept { return g_level; }

    const char* LevelName() noexcept
    {
        switch (g_level)
        {
        case Level::AVX10_2:  return "AVX10.2";
        case Level::AVX512:   return "AVX-512F";
        case Level::AVX2:     return "AVX2";
        default:              return "Fallback";
        }
    }

    void Fill32(void* dst, uint32_t val, size_t count) noexcept
    {
        if (!dst || count == 0) return;

        uintptr_t addr = reinterpret_cast<uintptr_t>(dst);

        switch (g_level)
        {
        case Level::AVX10_2:
        case Level::AVX512:
            if ((addr & 63) == 0)
                { fill32_avx512(dst, val, count); return; }
            [[fallthrough]];

        case Level::AVX2:
            if ((addr & 31) == 0)
                { fill32_avx2(dst, val, count); return; }
            [[fallthrough]];

        default:
            __stosd(static_cast<DWORD*>(dst), val,
                    static_cast<DWORD>(count));
        }
    }
}