@echo off
rem Rebuild the release tree the test harnesses run against. Pure ASCII.
setlocal
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
set CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe

rem A running copy holds the .exe open and the link fails with LNK1168.
taskkill /IM oriel.exe /F >nul 2>&1

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
"%CMAKE%" --build "%~dp0build-rel" || exit /b 1
echo.
echo built: %~dp0build-rel\oriel.exe
