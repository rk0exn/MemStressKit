#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Reporter
{
    void Init();
    void PrintSystemMemory();
    void OnNewHandlerFired();
    void OnVEH(EXCEPTION_POINTERS* ep);

    // SEH フィルタ: EXCEPTION_EXECUTE_HANDLER を返すと __except ブロックへ
    LONG SEHFilter(DWORD code, EXCEPTION_POINTERS* ep);
}
