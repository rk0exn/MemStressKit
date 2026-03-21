# MemStressKit

**Windows OOM / heap-stress debug toolkit**  
**Windows OOM・ヒープストレステスト デバッグツールキット**

---

## Overview / 概要

**[English]**

MemStressKit is a Windows-only toolkit for deliberately stressing heap and virtual memory to reproduce and analyze out-of-memory (OOM) conditions, heap corruption, and related exceptions — all while attached to a debugger.

It consists of three executables:

| Executable | Role |
|---|---|
| `memstresskit.exe` | Build & run orchestrator (the entry point) |
| `MemStresser.exe` | The actual stress tool — runs 6 memory-stress scenarios |
| `DummyDebugger.exe` | Thin Win32 debugger that satisfies `IsDebuggerPresent()` |

**[日本語]**

MemStressKit は Windows 専用の OOM（メモリ不足）・ヒープストレステスト用デバッグツールキットです。  
ヒープおよび仮想メモリを意図的に枯渇させることで、OOM・ヒープ破壊・関連例外を再現・解析します。デバッガへのアタッチが前提です。

実行ファイルは 3 つで構成されます：

| 実行ファイル | 役割 |
|---|---|
| `memstresskit.exe` | ビルド・実行オーケストレーター（エントリポイント） |
| `MemStresser.exe` | 本体 — 6 種類のメモリストレスシナリオを実行 |
| `DummyDebugger.exe` | `IsDebuggerPresent()` を満たす薄い Win32 デバッガ |

---

## Requirements / 動作要件

**[English]**

- Windows 10 x64 or later
- Visual Studio 2022 (or 2019) with the **Desktop development with C++** workload
- **x64 Native Tools Command Prompt for VS 20xx** (required for `cl.exe`)
- CPU with AVX2 or better (AVX-512 / AVX10.2 also supported, auto-detected at runtime)

**[日本語]**

- Windows 10 x64 以降
- Visual Studio 2022（または 2019）— **C++ によるデスクトップ開発** ワークロード必須
- **x64 Native Tools Command Prompt for VS 20xx**（`cl.exe` のパス解決に必要）
- AVX2 以上の CPU（AVX-512 / AVX10.2 も自動検出・対応済み）

---

## Quick Start / クイックスタート

### 1. Bootstrap / ブートストラップ

Open an **x64 Native Tools Command Prompt** and run:

```bat
setup.bat
```

This compiles `memstresskit.exe` from `src\MemStressKit.cpp` using a single `cl.exe` invocation.

**[日本語]** x64 Native Tools Command Prompt を開き `setup.bat` を実行します。  
`src\MemStressKit.cpp` をコンパイルして `memstresskit.exe` を生成します。

---

### 2. Build / ビルド

```bat
memstresskit build debug
memstresskit build release
memstresskit build release avx2 # AVX2 でビルド
memstresskit build release avx512 # AVX512 でビルド
memstresskit build release avx10 # AVX10.2 でビルド
memstresskit build all        # both / 両方
```

Outputs go to `Debug\` or `Release\` next to `memstresskit.exe`.

**[日本語]** ビルド成果物は `Debug\` または `Release\` に出力されます。

---

### 3. Run / 実行

```bat
memstresskit run debug              # DummyDebugger でアタッチして実行
memstresskit run release
memstresskit build release avx2     # AVX2 で実行
memstresskit build release avx512   # AVX512 で実行
memstresskit build release avx10    # AVX10.2 で実行
memstresskit run debug windbg       # WinDbg でアタッチして実行
```

When launched without `windbg`, `DummyDebugger.exe` attaches as the debugger so `IsDebuggerPresent()` returns `TRUE` inside `MemStresser.exe`.  
With `windbg`, WinDbg is launched with `-g -o` flags.

**[日本語]** `windbg` を指定しない場合は `DummyDebugger.exe` がデバッガとしてアタッチされ、`MemStresser.exe` 内で `IsDebuggerPresent()` が `TRUE` を返します。  
`windbg` を指定すると `-g -o` フラグ付きで WinDbg が起動します。

---

### 4. System Info / システム情報

```bat
memstresskit info
```

Prints OS version, CPU architecture, physical/virtual memory stats, page file usage, and current process memory.

**[日本語]** OS バージョン、CPU アーキテクチャ、物理・仮想メモリの使用状況、ページファイル使用量、プロセスメモリを表示します。

---

## Stress Scenarios / ストレスシナリオ

MemStresser presents an interactive menu with the following scenarios:

| # | Name | Description / 説明 |
|---|---|---|
| 1 | `SingleLargeBlock` | Allocates progressively larger `VirtualAlloc` blocks until OOM. SIMD-filled with `0xDEADBEEF`. / `VirtualAlloc` でブロックを倍増させながら OOM まで確保。SIMD で `0xDEADBEEF` 書き込み。 |
| 2 | `FragmentHeap` | Allocates 1M small `malloc` blocks, frees odd indices, then re-allocates 512B into the gaps to produce fragmentation. / 100万個の小ブロックを確保し奇数インデックスを解放後、512B で再割り当てしてフラグメントを発生させる。 |
| 3 | `STLContainerExplosion` | Grows a `vector<vector<char>>` until `std::bad_alloc` is thrown. / `std::bad_alloc` が送出されるまで `vector<vector<char>>` を拡張。 |
| 4 | `HeapExhaustBadAlloc` | Loops `new char[64MB]` until exhaustion, registering each block in the runtime tracker. / `new char[64MB]` をループしてヒープを枯渇させ、各ブロックをランタイムトラッカーに登録。 |
| 5 | `MultiThreadedRace` | Runs 8 `std::jthread` workers for 5 seconds, each racing on `malloc`/`free` to create heap contention. / 8 本の `std::jthread` が 5 秒間 `malloc`/`free` を競合させヒープ競合を発生させる。 |
| 6 | `AWELargePages` | Attempts a large-page `VirtualAlloc` (requires `SeLockMemoryPrivilege`). Falls back to normal pages if privilege is absent. / ラージページ `VirtualAlloc` を試みる（`SeLockMemoryPrivilege` 必要）。権限なしの場合は通常ページにフォールバック。 |

---

## Architecture / アーキテクチャ

```
memstresskit.exe  (orchestrator)
├── src\MemStressKit.cpp   -- CmdBuild / CmdRun / CmdInfo
│
├── src\main.cpp           -- entry point, VEH, SEH filter, menu loop
├── src\stress.cpp/h       -- the 6 stress scenarios
├── src\simd.cpp/h         -- CPUID detection, AVX2/AVX-512/AVX10.2 Fill32
├── src\reporter.cpp/h     -- heap-safe output (WriteFile), memory stats
├── src\runtime.cpp/h      -- resource registry, parent watchdog, fallsafe shutdown
├── src\ansicolor.h        -- ANSI 24-bit color macros, VT-mode setup
│
├── src\DummyDebugger.c    -- minimal Win32 debugger (DEBUG_PROCESS loop)
├── src\WaitForEvent.c     -- helper: polls a named event until signalled
│
└── src\app.manifest       -- UAT manifest (no elevation required)
```

### Key design points / 設計上のポイント

**[English]**

- **Heap-safe I/O in OOM paths** — `reporter.cpp` uses `WriteFile` + `wsprintfA` (no CRT heap allocation) inside VEH and SEH filters, where `printf` could deadlock or fail.
- **SIMD dispatch** — `simd.cpp` detects AVX10.2 / AVX-512F / AVX2 at runtime via CPUID leaf 7 and 24H; `Fill32` selects the optimal path and falls back to `__stosd`.
- **Resource registry** — `AppRuntime` tracks all allocated blocks under an `SRWLOCK`. On unhandled exception or parent-process exit, `Shutdown()` is called to release everything.
- **Watchdog thread** — hides itself from the debugger via `NtSetInformationThread(ThreadHideFromDebugger)` and polls the parent PID every 100 ms. If the parent exits, the child is terminated immediately.
- **DummyDebugger** — a `SUBSYSTEM:WINDOWS` process that attaches to `MemStresser.exe` via `DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE` and runs a minimal debug-event loop. `DebugSetProcessKillOnExit(FALSE)` ensures the target outlives the debugger process if needed.

**[日本語]**

- **OOM パスでのヒープ安全な I/O** — VEH・SEH フィルタ内では `printf` がデッドロック・失敗する可能性があるため、`reporter.cpp` は `WriteFile` + `wsprintfA`（CRT ヒープ不使用）を使用。
- **SIMD ディスパッチ** — `simd.cpp` が CPUID leaf 7 / 24H で AVX10.2 / AVX-512F / AVX2 をランタイム検出。`Fill32` は最適パスを選択し `__stosd` にフォールバック。
- **リソースレジストリ** — `AppRuntime` が `SRWLOCK` 下で全確保ブロックを追跡。未処理例外や親プロセス終了時に `Shutdown()` を呼び出して全解放。
- **ウォッチドッグスレッド** — `NtSetInformationThread(ThreadHideFromDebugger)` でデバッガから隠蔽し、100ms 間隔で親 PID をポーリング。親が終了すると子プロセスを即座に `TerminateProcess`。
- **DummyDebugger** — `SUBSYSTEM:WINDOWS` プロセスが `DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE` で `MemStresser.exe` にアタッチし、最小限のデバッグイベントループを実行。`DebugSetProcessKillOnExit(FALSE)` により必要に応じてターゲットがデバッガより長く生存可能。

---

## File Layout / ファイル構成

```
.
├── setup.bat              -- bootstrap: builds memstresskit.exe
├── src\
│   ├── app.manifest
│   ├── ansicolor.h
│   ├── main.cpp
│   ├── stress.cpp / stress.h
│   ├── simd.cpp   / simd.h
│   ├── reporter.cpp / reporter.h
│   ├── runtime.cpp  / runtime.h
│   ├── MemStressKit.cpp
│   ├── DummyDebugger.c
│   ├── WaitForEvent.c
│   ├── MemStresser.ico
│   ├── DummyDebugger.ico
│   └── MemStressKit.ico
├── Debug\                 -- created by: memstresskit build debug
│   ├── MemStresser.exe
│   ├── MemStresser.pdb
│   └── DummyDebugger.exe
└── Release\               -- created by: memstresskit build release
    ├── MemStresser.exe
    └── DummyDebugger.exe
```

---

## License / ライセンス

[CC BY-NC-SA 4.0](LICENSE.md) — Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International

You may use, share, and adapt this work for non-commercial purposes, provided you give appropriate credit and distribute any derivatives under the same license.

本ソフトウェアは [CC BY-NC-SA 4.0](LICENSE.md) のもとで提供されます。非商用目的に限り、適切なクレジット表記のもとで自由に使用・共有・改変できます。改変物も同一ライセンスで配布してください。
