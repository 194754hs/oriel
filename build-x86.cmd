@echo off
rem Builds the 32-bit shim only.
rem
rem A 64-bit in-proc COM server cannot load into a 32-bit process, so without
rem this every 32-bit application keeps getting the genuine dialog. The main
rem executable stays 64-bit: only the shim has to match the host.
rem Pure ASCII.
setlocal
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
set CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set NINJA=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe

call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1

if not exist "%~dp0build-x86" mkdir "%~dp0build-x86"
"%CMAKE%" -S "%~dp0." -B "%~dp0build-x86" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DORIEL_SHIM_ONLY=ON || exit /b 1
"%CMAKE%" --build "%~dp0build-x86" --target oriel_dialog || exit /b 1
echo.
echo built: %~dp0build-x86\oriel_dialog.dll
