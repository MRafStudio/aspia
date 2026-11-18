@echo off
rem Aspia extreme build script.
rem   build-aspia.bat              - configure (if needed) + full build
rem   build-aspia.bat <targets>    - build only the listed targets
rem   set SKIP_CONFIGURE=1         - skip cmake configure, reuse cache
setlocal EnableDelayedExpansion
chcp 65001 >nul

set "PATH=C:\Program Files\CMake\bin;%PATH%"
cd /d D:\.VS\Projects\aspia-extreem
set "LOGDIR=builds\logs"
set "LOG=%LOGDIR%\build.log"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"

call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

rem vswhere 3.1.7 from the VS Installer dies with 0x80070008 (ERROR_NOT_ENOUGH_MEMORY) when the
rem build runs from session 0 (Hermes terminal runs as SYSTEM). Point vcpkg at Visual Studio directly
rem so it never forks vswhere.
if not defined VCPKG_VISUAL_STUDIO_PATH set "VCPKG_VISUAL_STUDIO_PATH=C:\Program Files\Microsoft Visual Studio\18\Professional"

if "%SKIP_CONFIGURE%"=="1" goto build

echo === CONFIGURE %DATE% %TIME% === >> "%LOG%"
cmake --preset local-win-x64 >> "%LOG%" 2>&1
if errorlevel 1 (
  echo CONFIGURE FAILED >> "%LOG%"
  echo CONFIGURE FAILED
  exit /b 1
)
echo CONFIGURE OK %TIME% >> "%LOG%"
echo CONFIGURE OK

:build
echo === BUILD %DATE% %TIME% === >> "%LOG%"
if "%~1"=="" (
  cmake --build builds\local-win-x64 --config Release -j 8 >> "%LOG%" 2>&1
) else (
  cmake --build builds\local-win-x64 --config Release -j 8 --target %* >> "%LOG%" 2>&1
)
if errorlevel 1 (
  echo BUILD FAILED %TIME% >> "%LOG%"
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK %TIME% >> "%LOG%"
echo BUILD OK
exit /b 0
