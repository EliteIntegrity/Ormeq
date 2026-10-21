@echo off
REM Build Ormeq. Double-click this, or run it from a terminal.
REM
REM   build.bat              build the SDL3 frontend and run it
REM   build.bat win32        build the win32 baseline and run it
REM   build.bat sdl3 norun   build only, don't launch
REM
REM This is a thin wrapper around build.ps1. It exists because Windows blocks
REM PowerShell scripts by default, and a .bat file is not subject to that.

setlocal

set FRONTEND=%1
if "%FRONTEND%"=="" set FRONTEND=sdl3

set RUNFLAG=-Run
if "%2"=="norun" set RUNFLAG=

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Frontend %FRONTEND% %RUNFLAG%

echo.
pause
