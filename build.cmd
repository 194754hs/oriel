@echo off
rem Configure + build Oriel. Keep this file pure ASCII.
setlocal
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
set CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist "%~dp0build" mkdir "%~dp0build"
"%CMAKE%" -S "%~dp0." -B "%~dp0build" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=%1 || exit /b 1
"%CMAKE%" --build "%~dp0build" || exit /b 1
echo.
echo built: %~dp0build\oriel.exe
