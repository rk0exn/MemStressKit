// DummyDebugger.c
//
// Mode 1 (default): DummyDebugger.exe <exe-path>
//   Launches <exe-path> with DEBUG_PROCESS so IsDebuggerPresent() returns TRUE.
//   Runs as SUBSYSTEM:WINDOWS -- no console window is created.
//   The child process gets its own visible console via CREATE_NEW_CONSOLE.
//
// Mode 2: DummyDebugger.exe --check
//   Checks whether the calling process has a debugger attached.
//   Exit code: 0 = attached, 1 = not attached.
//
// Build: cl /nologo /W3 /O1 /utf-8 DummyDebugger.c
//          /link /SUBSYSTEM:WINDOWS /OUT:DummyDebugger.exe

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Mode 2: --check
// ---------------------------------------------------------------------------
static int ModeCheck(void)
{
    if (IsDebuggerPresent()) return 0;
    BOOL remote = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote) && remote)
        return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// Mode 1: <exe-path>
// ---------------------------------------------------------------------------
static int ModeDebug(const char* exePath)
{
    char path[MAX_PATH];
    size_t len = strlen(exePath);
    if (len >= 2 && exePath[0] == '"' && exePath[len-1] == '"')
    {
        strncpy_s(path, sizeof(path), exePath+1, len-2);
        path[len-2] = '\0';
    }
    else
    {
        strncpy_s(path, sizeof(path), exePath, _TRUNCATE);
    }

    STARTUPINFOA        si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    // DEBUG_PROCESS         : this process becomes the debugger of the child.
    // DEBUG_ONLY_THIS_PROCESS: do not debug grandchildren.
    // CREATE_NEW_CONSOLE    : child gets its own visible console window.
    if (!CreateProcessA(path, NULL, NULL, NULL, FALSE,
                        DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE,
                        NULL, NULL, &si, &pi))
    {
        // No console to write to after FreeConsole; use a message box for errors.
        char msg[256];
        _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                    "CreateProcess failed: GLE=%lu\nPath: %s", GetLastError(), path);
        MessageBoxA(NULL, msg, "DummyDebugger", MB_ICONERROR);
        return 1;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DebugSetProcessKillOnExit(FALSE);

    // --- debug event loop ---------------------------------------------------
    DEBUG_EVENT de;

    for (;;)
    {
        if (!WaitForDebugEvent(&de, INFINITE))
            break;

        DWORD status = DBG_CONTINUE;

        switch (de.dwDebugEventCode)
        {
        case EXCEPTION_DEBUG_EVENT:
            if (de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT
                && de.u.Exception.dwFirstChance)
                status = DBG_CONTINUE;
            else
                status = DBG_EXCEPTION_NOT_HANDLED;
            break;

        case EXIT_PROCESS_DEBUG_EVENT:
            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
            return 0;

        case CREATE_PROCESS_DEBUG_EVENT:
            if (de.u.CreateProcessInfo.hFile)
                CloseHandle(de.u.CreateProcessInfo.hFile);
            break;

        case LOAD_DLL_DEBUG_EVENT:
            if (de.u.LoadDll.hFile)
                CloseHandle(de.u.LoadDll.hFile);
            break;

        default:
            break;
        }

        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, status);
    }

    return 1;
}

// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    if (__argc >= 2 && strcmp(__argv[1], "--check") == 0)
        return ModeCheck();

    if (__argc >= 2)
        return ModeDebug(__argv[1]);

    MessageBoxA(NULL,
                "Usage:\n"
                "  DummyDebugger.exe <exe-path>   launch as debugger\n"
                "  DummyDebugger.exe --check       check if debugger attached",
                "DummyDebugger", MB_ICONINFORMATION);
    return 1;
}