@echo off
setlocal EnableExtensions
rem ===========================================================================
rem  qMDView - build an MSIX package for the Microsoft Store
rem
rem  Steps: build_installer.bat noinstaller  (Release build + windeployqt)
rem         -- copy into an MSIX layout with Assets and AppxManifest.xml
rem         -- makepri (scaled logos)  -- makeappx pack
rem
rem  Usage:  build_msix.bat [clean] [register] [nopause]
rem    clean     full rebuild (passed on to build_installer.bat)
rem    register  also install the built layout on this PC for testing
rem              (needs Windows Developer Mode; no certificate needed)
rem    nopause   do not wait for a key at the end
rem
rem  Output:
rem    dist\qMDView-<x.y.z.0>-x64.msix   upload this file to Partner Center
rem              (unsigned on purpose: the Store signs it after certification)
rem
rem  Before the first build fill in msix\store_identity.cmd with the values from
rem  Partner Center / Product management / Product identity.
rem  Needs the Windows 10/11 SDK (makeappx.exe, makepri.exe), which Visual Studio
rem  Installer can add: Individual components / Windows 11 SDK.
rem ===========================================================================

rem Must be the same WORK_DIR as in build_installer.bat
set "WORK_DIR=%LOCALAPPDATA%\qMDView_ms-build"
rem Optional: folder that holds makeappx.exe and makepri.exe
rem set "WINSDK_BIN=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"

set "DO_CLEAN="
set "DO_REGISTER="
set "NO_PAUSE="
for %%A in (%*) do (
    if /i "%%~A"=="clean"    set "DO_CLEAN=1"
    if /i "%%~A"=="register" set "DO_REGISTER=1"
    if /i "%%~A"=="nopause"  set "NO_PAUSE=1"
    if /i "%%~A"=="help"     goto :usage
    if /i "%%~A"=="/?"       goto :usage
)

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "MSIX_SRC=%PROJECT_DIR%\msix"
set "DEPLOY_DIR=%WORK_DIR%\deploy\qMDView"
set "MSIX_WORK=%WORK_DIR%\msix"
set "LAYOUT_DIR=%MSIX_WORK%\layout"
set "PRI_ROOT=%MSIX_WORK%\priroot"
set "PRI_CFG=%MSIX_WORK%\priconfig.xml"
set "MSIX_TEMPLATE=%MSIX_SRC%\AppxManifest.xml.in"
set "MANIFEST_OUT=%LAYOUT_DIR%\AppxManifest.xml"
set "DIST_DIR=%PROJECT_DIR%\dist"

echo.
echo ===================== qMDView MSIX package =====================

rem ---------------------------------------------------------------------------
rem  [0] Store identity, version, SDK tools
rem ---------------------------------------------------------------------------
set "MSIX_IDENTITY_NAME="
set "MSIX_PUBLISHER="
set "MSIX_PUBLISHER_DISPLAY_NAME="
if not exist "%MSIX_SRC%\store_identity.cmd" goto :identity_missing
call "%MSIX_SRC%\store_identity.cmd"
if not defined MSIX_IDENTITY_NAME goto :identity_missing
if not defined MSIX_PUBLISHER goto :identity_missing
if not defined MSIX_PUBLISHER_DISPLAY_NAME goto :identity_missing
if /i "%MSIX_IDENTITY_NAME%"=="REPLACE_ME" goto :identity_missing
if /i "%MSIX_PUBLISHER%"=="REPLACE_ME" goto :identity_missing
goto :identity_ok
:identity_missing
set "FAIL_MSG=Fill in msix\store_identity.cmd with the 3 values from Partner Center (Product identity page)"
goto :fail
:identity_ok

rem Version from CMakeLists.txt  project(qMDView VERSION 1.2.3 ...)  becomes 1.2.3.0
set "APP_VERSION="
for /f "tokens=4 delims=() " %%V in ('findstr /c:"project(qMDView VERSION" "%PROJECT_DIR%\CMakeLists.txt"') do set "APP_VERSION=%%V"
if not defined APP_VERSION (
    set "FAIL_MSG=Cannot read the version from CMakeLists.txt"
    goto :fail
)
set "V_MAJOR="
set "V_MINOR="
set "V_PATCH="
for /f "tokens=1-3 delims=." %%a in ("%APP_VERSION%") do (
    set "V_MAJOR=%%a"
    set "V_MINOR=%%b"
    set "V_PATCH=%%c"
)
if not defined V_MINOR set "V_MINOR=0"
if not defined V_PATCH set "V_PATCH=0"
set "MSIX_VERSION=%V_MAJOR%.%V_MINOR%.%V_PATCH%.0"
set "MSIX_FILE=%DIST_DIR%\qMDView-%MSIX_VERSION%-x64.msix"

call :find_sdk_tool makeappx.exe MAKEAPPX
call :find_sdk_tool makepri.exe MAKEPRI
if not defined MAKEAPPX goto :sdk_missing
if not defined MAKEPRI goto :sdk_missing
goto :sdk_ok
:sdk_missing
set "FAIL_MSG=makeappx.exe / makepri.exe not found. Install the Windows 11 SDK with Visual Studio Installer (Individual components), or set WINSDK_BIN at the top of this file"
goto :fail
:sdk_ok

echo  Identity : %MSIX_IDENTITY_NAME%
echo  Publisher: %MSIX_PUBLISHER%
echo  Display  : %MSIX_PUBLISHER_DISPLAY_NAME%
echo  Version  : %MSIX_VERSION%
echo  makeappx : %MAKEAPPX%
echo.

rem ---------------------------------------------------------------------------
rem  [1] Release build + windeployqt (same steps as the Inno Setup installer)
rem ---------------------------------------------------------------------------
echo [1/4] Release build + deploy (build_installer.bat noinstaller) ...
set "BUILD_ARGS=noinstaller nopause"
if defined DO_CLEAN set "BUILD_ARGS=clean noinstaller nopause"
call "%PROJECT_DIR%\build_installer.bat" %BUILD_ARGS%
if errorlevel 1 (
    set "FAIL_MSG=Build or windeployqt failed (see errors above)"
    goto :fail
)
if not exist "%DEPLOY_DIR%\qMDView.exe" (
    set "FAIL_MSG=qMDView.exe not found in the deploy folder %DEPLOY_DIR%"
    goto :fail
)

rem ---------------------------------------------------------------------------
rem  [2] MSIX layout: deployed files + Assets + AppxManifest.xml
rem ---------------------------------------------------------------------------
echo.
echo [2/4] Preparing the MSIX layout ...
if exist "%MSIX_WORK%" rmdir /s /q "%MSIX_WORK%"
if exist "%LAYOUT_DIR%\qMDView.exe" (
    set "FAIL_MSG=Cannot clean %MSIX_WORK% - close qMDView first"
    goto :fail
)
mkdir "%LAYOUT_DIR%"
xcopy "%DEPLOY_DIR%" "%LAYOUT_DIR%\" /e /i /y /q >nul
if errorlevel 1 (
    set "FAIL_MSG=Cannot copy the deploy folder into the MSIX layout"
    goto :fail
)
xcopy "%MSIX_SRC%\Assets" "%LAYOUT_DIR%\Assets\" /e /i /y /q >nul
if errorlevel 1 (
    set "FAIL_MSG=Cannot copy msix\Assets"
    goto :fail
)
rem Fill the @TOKENS@ of the template (values are XML-escaped; output is UTF-8 without BOM)
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $t=[IO.File]::ReadAllText($env:MSIX_TEMPLATE); foreach($k in 'IDENTITY_NAME','PUBLISHER_DISPLAY_NAME','PUBLISHER','VERSION'){ $v=[Environment]::GetEnvironmentVariable('MSIX_'+$k); $t=$t.Replace('@'+$k+'@',[Security.SecurityElement]::Escape($v)) }; if($t -cmatch '@[A-Z_]+@'){ throw 'unfilled token in manifest' }; [IO.File]::WriteAllText($env:MANIFEST_OUT,$t,(New-Object Text.UTF8Encoding $false))"
if errorlevel 1 (
    set "FAIL_MSG=Cannot create AppxManifest.xml from msix\AppxManifest.xml.in"
    goto :fail
)
if not exist "%MANIFEST_OUT%" (
    set "FAIL_MSG=AppxManifest.xml was not created (is Windows PowerShell available?)"
    goto :fail
)

rem ---------------------------------------------------------------------------
rem  [3] resources.pri - lets Windows pick the right logo size (scale / targetsize)
rem ---------------------------------------------------------------------------
echo.
echo [3/4] Indexing logo resources (makepri) ...
mkdir "%PRI_ROOT%"
xcopy "%MSIX_SRC%\Assets" "%PRI_ROOT%\Assets\" /e /i /y /q >nul
"%MAKEPRI%" createconfig /cf "%PRI_CFG%" /dq zh-TW /pv 10.0.0 /o >nul
if errorlevel 1 (
    set "FAIL_MSG=makepri createconfig failed"
    goto :fail
)
rem Drop the <packaging> section so that one resources.pri is produced (no resource packs)
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $c=[IO.File]::ReadAllText($env:PRI_CFG); $c=[regex]::Replace($c,'(?s)<packaging>.*?</packaging>',''); [IO.File]::WriteAllText($env:PRI_CFG,$c,(New-Object Text.UTF8Encoding $false))"
if errorlevel 1 (
    set "FAIL_MSG=Cannot edit priconfig.xml"
    goto :fail
)
"%MAKEPRI%" new /pr "%PRI_ROOT%" /cf "%PRI_CFG%" /mn "%MANIFEST_OUT%" /of "%LAYOUT_DIR%\resources.pri" /o
if errorlevel 1 (
    set "FAIL_MSG=makepri new failed (see errors above)"
    goto :fail
)

rem ---------------------------------------------------------------------------
rem  [4] Pack (makeappx also validates AppxManifest.xml against the schema)
rem ---------------------------------------------------------------------------
echo.
echo [4/4] Packing MSIX (makeappx) ...
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
if exist "%MSIX_FILE%" del /f /q "%MSIX_FILE%"
"%MAKEAPPX%" pack /d "%LAYOUT_DIR%" /p "%MSIX_FILE%" /o
if errorlevel 1 (
    set "FAIL_MSG=makeappx pack failed (see errors above)"
    goto :fail
)
if not exist "%MSIX_FILE%" (
    set "FAIL_MSG=MSIX file was not produced"
    goto :fail
)

if not defined DO_REGISTER goto :register_done
echo.
echo [test] Installing the layout on this PC (Developer Mode required) ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; Get-AppxPackage -Name $env:MSIX_IDENTITY_NAME | Remove-AppxPackage; Add-AppxPackage -Register $env:MANIFEST_OUT"
if errorlevel 1 (
    set "FAIL_MSG=Add-AppxPackage failed. Turn on Settings / System / For developers / Developer Mode and run: build_msix.bat register"
    goto :fail
)
echo        Installed. Start qMDView from the Start menu to test the packaged app.
:register_done

echo.
echo ============================== DONE ==============================
echo  MSIX   : %MSIX_FILE%
echo  Layout : %LAYOUT_DIR%
echo  Upload the .msix in Partner Center / Submission / Packages.
echo ==================================================================
if defined NO_PAUSE exit /b 0
explorer.exe /select,"%MSIX_FILE%"
pause
exit /b 0

:fail
echo.
echo *** FAILED: %FAIL_MSG%
if not defined NO_PAUSE pause
exit /b 1

rem ---------------------------------------------------------------------------
rem  :find_sdk_tool <exe name> <variable>  - newest Windows SDK x64 copy wins
rem ---------------------------------------------------------------------------
:find_sdk_tool
set "%~2="
if defined WINSDK_BIN if exist "%WINSDK_BIN%\%~1" set "%~2=%WINSDK_BIN%\%~1"
if defined %~2 goto :eof
set "SDK_BIN_ROOT=%ProgramFiles(x86)%\Windows Kits\10\bin"
if not exist "%SDK_BIN_ROOT%\" goto :find_sdk_where
pushd "%SDK_BIN_ROOT%"
for /d %%D in (10.*) do if exist "%%~fD\x64\%~1" set "%~2=%%~fD\x64\%~1"
popd
if defined %~2 goto :eof
:find_sdk_where
for /f "delims=" %%P in ('where %~1 2^>nul') do if not defined %~2 set "%~2=%%P"
goto :eof

:usage
echo Usage: build_msix.bat [clean] [register] [nopause]
echo   clean     full rebuild
echo   register  install the built layout on this PC for testing (Developer Mode)
echo   nopause   do not wait for a key at the end
exit /b 0
