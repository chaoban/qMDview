@echo off
setlocal EnableExtensions
rem ===========================================================================
rem  qMDView - build the Release exe and package a Windows installer
rem
rem  Steps: CMake/Ninja + MinGW build -> windeployqt -> Inno Setup (ISCC)
rem
rem  Usage:  build_installer.bat [clean] [noinstaller] [nopause]
rem    clean        delete previous build output first (full rebuild)
rem    noinstaller  build + deploy only (portable folder), skip Inno Setup
rem    nopause      do not wait for a key at the end (for scripts / CI)
rem
rem  Output:
rem    dist\qMDView-<version>-setup.exe    installer for other PCs
rem    %LOCALAPPDATA%\qMDView_ms-build\deploy\qMDView\   portable folder
rem
rem  The version number is read from project(... VERSION x.y.z) in CMakeLists.txt
rem ===========================================================================

rem ---- Settings: adjust if your Qt installation is different ----------------
set "QT_ROOT=C:\Qt"
set "QT_VERSION=6.6.2"
set "QT_KIT=mingw_64"
rem MinGW toolchain that matches the Qt kit (Qt 6.6 = MinGW 11.2)
set "MINGW_NAME=mingw1120_64"
rem Build files are kept outside OneDrive so thousands of objects are not synced.
rem qMDView_ms-build: separate from the original qMDView project, whose CMake
rem cache in qMDView-build points at a different source folder.
set "WORK_DIR=%LOCALAPPDATA%\qMDView_ms-build"
rem Optional: full path to ISCC.exe if Inno Setup is installed somewhere unusual
rem set "ISCC_PATH=D:\Tools\Inno Setup 7\ISCC.exe"
rem ---------------------------------------------------------------------------

set "DO_CLEAN="
set "NO_INSTALLER="
set "NO_PAUSE="
for %%A in (%*) do (
    if /i "%%~A"=="clean"       set "DO_CLEAN=1"
    if /i "%%~A"=="noinstaller" set "NO_INSTALLER=1"
    if /i "%%~A"=="nopause"     set "NO_PAUSE=1"
    if /i "%%~A"=="help"        goto :usage
    if /i "%%~A"=="/?"          goto :usage
)

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "BUILD_DIR=%WORK_DIR%\build"
set "DEPLOY_DIR=%WORK_DIR%\deploy\qMDView"
set "DIST_DIR=%PROJECT_DIR%\dist"
set "APP_EXE=qMDView.exe"

echo.
echo ==================== qMDView build ^& package ====================
echo  Project : %PROJECT_DIR%
echo  Work dir: %WORK_DIR%

rem ---------------------------------------------------------------------------
rem  [0] Locate tools
rem ---------------------------------------------------------------------------
set "QT_DIR=%QT_ROOT%\%QT_VERSION%\%QT_KIT%"
if exist "%QT_DIR%\bin\windeployqt.exe" goto :qt_found
rem Configured version not found: use a C:\Qt\6.x that has this kit (last match wins)
set "QT_DIR="
if not exist "%QT_ROOT%\" goto :qt_search_done
pushd "%QT_ROOT%"
for /d %%D in (6.*) do if exist "%%~fD\%QT_KIT%\bin\windeployqt.exe" set "QT_DIR=%%~fD\%QT_KIT%"
popd
:qt_search_done
if not defined QT_DIR (
    set "FAIL_MSG=Qt kit not found: %QT_ROOT%\%QT_VERSION%\%QT_KIT%  (edit QT_ROOT / QT_VERSION at the top of this file)"
    goto :fail
)
:qt_found

set "MINGW_DIR=%QT_ROOT%\Tools\%MINGW_NAME%"
if exist "%MINGW_DIR%\bin\g++.exe" goto :mingw_found
set "MINGW_DIR="
if not exist "%QT_ROOT%\Tools\" goto :mingw_search_done
pushd "%QT_ROOT%\Tools"
for /d %%D in (mingw*_64) do if exist "%%~fD\bin\g++.exe" set "MINGW_DIR=%%~fD"
popd
:mingw_search_done
if not defined MINGW_DIR (
    set "FAIL_MSG=MinGW not found under %QT_ROOT%\Tools  (install it with the Qt Maintenance Tool)"
    goto :fail
)
:mingw_found

set "CMAKE_EXE=%QT_ROOT%\Tools\CMake_64\bin\cmake.exe"
if exist "%CMAKE_EXE%" goto :cmake_found
set "CMAKE_EXE="
for /f "delims=" %%P in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%P"
if not defined CMAKE_EXE (
    set "FAIL_MSG=cmake.exe not found (install CMake from the Qt Maintenance Tool)"
    goto :fail
)
:cmake_found

set "NINJA_EXE=%QT_ROOT%\Tools\Ninja\ninja.exe"
if exist "%NINJA_EXE%" goto :ninja_found
set "NINJA_EXE="
for /f "delims=" %%P in ('where ninja.exe 2^>nul') do if not defined NINJA_EXE set "NINJA_EXE=%%P"
if not defined NINJA_EXE (
    set "FAIL_MSG=ninja.exe not found (install Ninja from the Qt Maintenance Tool)"
    goto :fail
)
:ninja_found

rem Version from CMakeLists.txt:  project(qMDView VERSION 1.0.0 LANGUAGES CXX)
set "APP_VERSION="
for /f "tokens=4 delims=() " %%V in ('findstr /c:"project(qMDView VERSION" "%PROJECT_DIR%\CMakeLists.txt"') do set "APP_VERSION=%%V"
if not defined APP_VERSION set "APP_VERSION=1.0.0"

echo  Qt      : %QT_DIR%
echo  MinGW   : %MINGW_DIR%
echo  CMake   : %CMAKE_EXE%
echo  Version : %APP_VERSION%
echo.

rem Compilers first in PATH so another gcc (Strawberry Perl, MSYS2...) is not picked up
set "PATH=%QT_DIR%\bin;%MINGW_DIR%\bin;%PATH%"

rem ---------------------------------------------------------------------------
rem  [1] Configure + build (Release)
rem ---------------------------------------------------------------------------
rem (goto-style instead of ( ) blocks: paths containing parentheses break blocks)
if not defined DO_CLEAN goto :clean_done
echo [clean] Removing "%WORK_DIR%"
if exist "%WORK_DIR%" rmdir /s /q "%WORK_DIR%"
:clean_done

echo [1/3] Building Release ...
set "QT_DIR_CM=%QT_DIR:\=/%"
set "NINJA_CM=%NINJA_EXE:\=/%"
set "CXX_CM=%MINGW_DIR:\=/%/bin/g++.exe"
"%CMAKE_EXE%" -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%QT_DIR_CM%" "-DCMAKE_MAKE_PROGRAM=%NINJA_CM%" "-DCMAKE_CXX_COMPILER=%CXX_CM%"
if errorlevel 1 (
    set "FAIL_MSG=CMake configure failed. If you switched Qt versions, run: build_installer.bat clean"
    goto :fail
)
"%CMAKE_EXE%" --build "%BUILD_DIR%" --parallel
if errorlevel 1 (
    set "FAIL_MSG=Compile failed (see errors above)"
    goto :fail
)
if not exist "%BUILD_DIR%\%APP_EXE%" (
    set "FAIL_MSG=%BUILD_DIR%\%APP_EXE% was not produced"
    goto :fail
)

rem ---------------------------------------------------------------------------
rem  [2] Deploy: exe + Qt DLLs + plugins + MinGW runtime into one folder
rem ---------------------------------------------------------------------------
echo.
echo [2/3] Deploying Qt runtime with windeployqt ...
if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%"
mkdir "%DEPLOY_DIR%"
copy /y "%BUILD_DIR%\%APP_EXE%" "%DEPLOY_DIR%\" >nul
if errorlevel 1 (
    set "FAIL_MSG=Cannot copy %APP_EXE% to %DEPLOY_DIR% (is qMDView still running?)"
    goto :fail
)
rem --no-opengl-sw / --no-system-d3d-compiler: not needed by a Widgets app (saves ~25 MB)
"%QT_DIR%\bin\windeployqt.exe" --release --compiler-runtime --no-opengl-sw --no-system-d3d-compiler --translations zh_TW --verbose 0 "%DEPLOY_DIR%\%APP_EXE%"
if errorlevel 1 (
    set "FAIL_MSG=windeployqt failed"
    goto :fail
)
rem License texts (Qt LGPL obligations) - also viewable in Help > About > Licenses
xcopy "%PROJECT_DIR%\licenses" "%DEPLOY_DIR%\licenses\" /e /i /y /q >nul
if errorlevel 1 (
    set "FAIL_MSG=Cannot copy the licenses folder"
    goto :fail
)
rem Chinese captions for Qt's standard buttons (Yes/No/Cancel...)
if not exist "%DEPLOY_DIR%\translations\qtbase_zh_TW.qm" if exist "%QT_DIR%\translations\qtbase_zh_TW.qm" (
    if not exist "%DEPLOY_DIR%\translations" mkdir "%DEPLOY_DIR%\translations"
    copy /y "%QT_DIR%\translations\qtbase_zh_TW.qm" "%DEPLOY_DIR%\translations\" >nul
)
if not exist "%DEPLOY_DIR%\Qt6Core.dll" (
    set "FAIL_MSG=Qt6Core.dll missing in %DEPLOY_DIR% - deployment incomplete"
    goto :fail
)
echo       Portable folder: %DEPLOY_DIR%

if defined NO_INSTALLER goto :done

rem ---------------------------------------------------------------------------
rem  [3] Package with Inno Setup
rem ---------------------------------------------------------------------------
echo.
echo [3/3] Creating installer with Inno Setup ...
call :find_iscc
if defined ISCC goto :iscc_found

echo       Inno Setup was not found on this PC.
if defined NO_PAUSE goto :no_iscc
where winget.exe >nul 2>nul
if errorlevel 1 goto :no_iscc
choice /c YN /m "      Install Inno Setup 7 now with winget (current user, free)"
if errorlevel 2 goto :no_iscc
winget install --id JRSoftware.InnoSetup.7 --exact --scope user --accept-source-agreements --accept-package-agreements
if errorlevel 1 winget install --id JRSoftware.InnoSetup.7 --exact --accept-source-agreements --accept-package-agreements
call :find_iscc
if defined ISCC goto :iscc_found

:no_iscc
echo.
echo       Skipped: install Inno Setup 7 from https://jrsoftware.org/isdl.php
echo       (or: winget install JRSoftware.InnoSetup.7) and run this file again.
echo       The portable folder above already runs on other PCs as-is.
set "FAIL_MSG=Inno Setup (ISCC.exe) not found"
goto :fail

:iscc_found
echo       ISCC: %ISCC%
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
"%ISCC%" /Qp "/DAppVersion=%APP_VERSION%" "/DDeployDir=%DEPLOY_DIR%" "/O%DIST_DIR%" "%PROJECT_DIR%\installer\qMDView.iss"
if errorlevel 1 (
    set "FAIL_MSG=Inno Setup compile failed (see errors above)"
    goto :fail
)
set "SETUP_EXE=%DIST_DIR%\qMDView-%APP_VERSION%-setup.exe"
if not exist "%SETUP_EXE%" (
    set "FAIL_MSG=%SETUP_EXE% was not produced"
    goto :fail
)

:done
echo.
echo ============================== DONE ==============================
if not defined SETUP_EXE goto :done_no_setup
echo  Installer : %SETUP_EXE%
:done_no_setup
echo  Portable  : %DEPLOY_DIR%
echo ==================================================================
if defined NO_PAUSE exit /b 0
if defined SETUP_EXE explorer.exe /select,"%SETUP_EXE%"
pause
exit /b 0

:fail
echo.
echo *** FAILED: %FAIL_MSG%
if not defined NO_PAUSE pause
exit /b 1

rem ---------------------------------------------------------------------------
rem  Find ISCC.exe (Inno Setup 7 preferred: it ships the Traditional Chinese UI)
rem ---------------------------------------------------------------------------
:find_iscc
set "ISCC="
if defined ISCC_PATH if exist "%ISCC_PATH%" set "ISCC=%ISCC_PATH%"
if defined ISCC goto :eof
for %%P in (
    "%ProgramFiles%\Inno Setup 7\ISCC.exe"
    "%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe"
    "%LOCALAPPDATA%\Programs\Inno Setup 7\ISCC.exe"
    "%ProgramFiles%\Inno Setup 6\ISCC.exe"
    "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
    "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
) do if not defined ISCC if exist "%%~P" set "ISCC=%%~P"
if defined ISCC goto :eof
for /f "delims=" %%P in ('where ISCC.exe 2^>nul') do if not defined ISCC set "ISCC=%%P"
goto :eof

:usage
echo Usage: build_installer.bat [clean] [noinstaller] [nopause]
echo   clean        delete previous build output first (full rebuild)
echo   noinstaller  build + deploy only, skip Inno Setup
echo   nopause      do not wait for a key at the end
exit /b 0
