@echo off
rem ===========================================================================
rem  build_release.bat - one-click release build for qMDView
rem
rem    1. build_installer.bat clean nopause
rem         -> dist\qMDView-<x.y.z>-setup.exe      installer for testing / sharing
rem    2. build_msix.bat nopause
rem         -> dist\qMDView-<x.y.z>.0-x64.msix     upload this to Partner Center
rem
rem  Exit codes of each step are written to dist\build_release.log
rem  Keep this file ASCII-only with CRLF line endings.
rem  Redirections are written in front of echo so that a trailing digit
rem  (for example "exit code 0") is not taken as a handle number.
rem ===========================================================================
setlocal
set "PROJECT_DIR=%~dp0"
set "DIST_DIR=%PROJECT_DIR%dist"
set "LOG=%DIST_DIR%\build_release.log"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"

> "%LOG%" echo build_release started %date% %time%

echo.
echo ==== [1/2] Installer: build_installer.bat clean ====
call "%PROJECT_DIR%build_installer.bat" clean nopause
set "RC=%errorlevel%"
>> "%LOG%" echo build_installer.bat exit code %RC%
if not "%RC%"=="0" goto :fail

echo.
echo ==== [2/2] Microsoft Store package: build_msix.bat ====
call "%PROJECT_DIR%build_msix.bat" nopause
set "RC=%errorlevel%"
>> "%LOG%" echo build_msix.bat exit code %RC%
if not "%RC%"=="0" goto :fail

>> "%LOG%" echo build_release finished OK %date% %time%
dir "%DIST_DIR%" >> "%LOG%"
echo.
echo ==== Done. Output files in the dist folder: ====
dir /b "%DIST_DIR%"
echo.
pause
exit /b 0

:fail
>> "%LOG%" echo build_release FAILED
echo.
echo ==== FAILED - see the messages above ====
echo.
pause
exit /b 1
