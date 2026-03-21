// WaitForEvent.c
// Opens a named event and waits until it is signalled or timeout expires.
//
// Usage  : WaitForEvent.exe <EventName> [TimeoutMs]
//   TimeoutMs defaults to 10000 (10 seconds).
// Exit code: 0 = signalled, 1 = timeout or error
//
// Build  : cl /nologo /W3 /O1 /utf-8 WaitForEvent.c
//            /link /SUBSYSTEM:CONSOLE /OUT:WaitForEvent.exe

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: WaitForEvent.exe <EventName> [TimeoutMs]\n");
        return 1;
    }

    DWORD timeout    = (argc >= 3) ? (DWORD)strtoul(argv[2], NULL, 10) : 10000;
    DWORD pollMs     = 100;
    DWORD elapsed    = 0;
    HANDLE hEvent    = NULL;

    // Poll until the event exists or we exceed timeout.
    // DummyDebugger.exe is started asynchronously so the event may not
    // exist yet when WaitForEvent.exe first runs.
    while (elapsed < timeout)
    {
        hEvent = OpenEventA(SYNCHRONIZE, FALSE, argv[1]);
        if (hEvent) break;

        if (GetLastError() != ERROR_FILE_NOT_FOUND)
        {
            fprintf(stderr, "OpenEvent(\"%s\") failed: GLE=%lu\n",
                    argv[1], GetLastError());
            return 1;
        }

        Sleep(pollMs);
        elapsed += pollMs;
    }

    if (!hEvent)
    {
        fprintf(stderr, "OpenEvent(\"%s\") timed out after %lums\n",
                argv[1], timeout);
        return 1;
    }

    // Event exists; wait for it to be signalled.
    DWORD remaining = (elapsed < timeout) ? (timeout - elapsed) : 0;
    DWORD result    = WaitForSingleObject(hEvent, remaining);
    CloseHandle(hEvent);

    if (result == WAIT_OBJECT_0)
        return 0;

    fprintf(stderr, "WaitForEvent: timeout waiting for signal (result=%lu)\n", result);
    return 1;
}