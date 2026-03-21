#pragma once
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// SIMD capability detection and dispatch
// ---------------------------------------------------------------------------
namespace Simd
{
    enum class Level : int
    {
        Fallback = 0,   // __stosd
        AVX2     = 1,   // 256-bit YMM
        AVX512   = 2,   // 512-bit ZMM (AVX-512F)
        AVX10_2  = 3,   // AVX10.2 (superset of AVX-512)
    };

    // Detect CPU capabilities at runtime. Call once at startup.
    Level   Detect() noexcept;

    // Returns the level detected by the last Detect() call.
    Level   Current() noexcept;

    // Fill [dst, dst+count*4) with the 32-bit pattern `val`.
    // dst must be 64-byte aligned for AVX-512/AVX10.2, 32-byte for AVX2.
    // Falls back to __stosd if alignment is insufficient.
    void    Fill32(void* dst, uint32_t val, size_t count) noexcept;

    // Human-readable name of the current level.
    const char* LevelName() noexcept;
}