@echo off
rem MemStressKit setup -- builds memstresskit.exe only
rem Run from VS Developer Command Prompt.

setlocal

if "%1"=="/?" goto :Help
if "%1"=="-?" goto :Help

cd /d "%~dp0"

where cl.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cl.exe not found on PATH.
    echo         Run setup.bat from:
    echo           "x64 Native Tools Command Prompt for VS 20xx"
    echo         ^(Start menu -^> Visual Studio 20xx -^> x64 Native Tools Command Prompt^)
    exit /b 1
)

echo [Setup] Building memstresskit.exe ...

rem -- icon resource (skip silently if rc.exe or ico is absent) --
set RC_RES=
where rc.exe >nul 2>&1
if not errorlevel 1 (
    if exist src\MemStressKit.ico (
        echo 1 ICON "src\MemStressKit.ico" > memstresskit_icon.rc
        rc /nologo /fo memstresskit_icon.res memstresskit_icon.rc
        if not errorlevel 1 ( set RC_RES=memstresskit_icon.res )
        del /q memstresskit_icon.rc >nul 2>&1
    )
)

cl /nologo /EHsc /W4 /std:c++20 /utf-8 /MT src\memstresskit.cpp /link /MACHINE:X64 /SUBSYSTEM:CONSOLE /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest %RC_RES% /OUT:memstresskit.exe
if errorlevel 1 ( echo [FAILED] memstresskit.exe & exit /b 1 )

del /q memstresskit.obj >nul 2>&1
if defined RC_RES del /q %RC_RES% >nul 2>&1

echo.
echo [OK] memstresskit.exe
echo.
echo Next steps:
echo   memstresskit build debug
echo   memstresskit build release
echo   memstresskit run debug
exit /b 0

:Help
echo.
echo Usage: setup.bat
echo   Builds memstresskit.exe from memstresskit.cpp.
echo   Run from VS Developer Command Prompt (x64).
echo.
echo Options:
echo   /?  -?  Show this help
exit /b 0