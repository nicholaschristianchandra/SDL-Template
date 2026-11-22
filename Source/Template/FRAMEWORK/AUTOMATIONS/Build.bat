@echo off
setlocal disabledelayedexpansion

rem A ! in the path would break the delayed expansion used below
set "SCRIPT_FOLDER=%~dp0"
if not "%SCRIPT_FOLDER:!=%"=="%SCRIPT_FOLDER%" (
    echo "[Build][Error] The project folder's path contains a '!', which Windows batch files cannot handle. Rename or move the folder."
    exit /b 1
)

setlocal enabledelayedexpansion

rem Settings: Config.bat, then the arguments, Build.bat [Debug or Release] [x64, x86 or a platform folder]
for %%R in ("%~dp0..\..") do set "ROOT=%%~fR"
set "ORIGINAL_PATH=%PATH%"
call "%ROOT%\Config.bat"
set "TOOLS=%ROOT%\Builds\Cache\Tools.exe"
set "PLATFORMS=%ROOT%\Framework\Automations\Platforms"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
set "PLATFORM=%~2"
if "%PLATFORM%"=="" set "PLATFORM=x64"
if /i "%PLATFORM%"=="Win32" set "PLATFORM=x86"

rem The platform is a folder in Platforms (Android), or a CPU listed in a platform's PLATFORM_ARCHITECTURES (x64)
set "TARGET="
set "ARCHITECTURE="
pushd "%PLATFORMS%"
for /d %%P in (*) do if /i "%%P"=="%PLATFORM%" if exist "%%P\Build.bat" set "TARGET=%%P"
if not defined TARGET for /d %%P in (*) do if exist "%%P\Config.bat" call :MatchArchitecture "%%P"
popd
if not defined TARGET (
    echo "[Build][Error] Unknown platform %PLATFORM%: Framework\Automations\Platforms has no folder for it."
    exit /b 1
)

rem The platform's settings, from its Config.bat
set "PLATFORM_DIR=%PLATFORMS%\%TARGET%"
set "PLATFORM_ARCHITECTURES="
if exist "%PLATFORM_DIR%\Config.bat" call "%PLATFORM_DIR%\Config.bat"
if not defined ARCHITECTURE if defined PLATFORM_ARCHITECTURES for %%A in (%PLATFORM_ARCHITECTURES%) do if not defined ARCHITECTURE set "ARCHITECTURE=%%A"
if "%CONFIG%"=="Debug" if /i not "%TARGET%"=="Windows" (
    echo "[Build][Error] %TARGET% builds Release only."
    exit /b 1
)

rem Folders: the output, the compiled objects and SDL, per platform and CPU
set "PLATFORM_FOLDER=%TARGET%"
if defined ARCHITECTURE set "PLATFORM_FOLDER=%TARGET%\%ARCHITECTURE%"
set "BUILD_PATH=%ROOT%\Builds\Outputs\%PLATFORM_FOLDER%\%CONFIG%"
set "OBJECTS_PATH=%ROOT%\Builds\Cache\%PLATFORM_FOLDER%\Objects\%CONFIG%"
set "SDL_LIB_DIR=%ROOT%\Libraries\SDL3\lib\%PLATFORM_FOLDER%"
set "ENV_PLATFORM=x64"
if "%ARCHITECTURE%"=="x86" set "ENV_PLATFORM=x86"

rem A changed PROGRAM_NAME goes into the project, so Visual Studio runs the new name
pushd "%ROOT%"
for %%P in (*.vcxproj) do set "PROJECT_FILE=%%~fP"
popd
set "LOADED_NAME="
for /f "tokens=3 delims=<>" %%G in ('findstr /c:"<GameName>" "%PROJECT_FILE%"') do set "LOADED_NAME=%%G"
if not "!LOADED_NAME!"=="%PROGRAM_NAME%" (
    powershell -NoProfile -Command "$t = [IO.File]::ReadAllText($env:PROJECT_FILE); [IO.File]::WriteAllText($env:PROJECT_FILE, ($t -replace '<GameName>[^<]*</GameName>', ('<GameName>' + $env:PROGRAM_NAME + '</GameName>').Replace('$', '$$')))"
    echo "[Build][Error] PROGRAM_NAME changed to %PROGRAM_NAME%. Reload the project when Visual Studio asks, then build again."
    exit /b 1
)

rem The .hpp item template (Add, New Item), copied into Visual Studio once
if not exist "%ROOT%\Builds\Cache\ScriptTemplate.installed" call :InstallScriptTemplate

echo [Build 0%%] Starting %CONFIG% (%PLATFORM_FOLDER%) build...

rem Visual Studio's compilers (vcvarsall.bat), cached in Builds\Cache\Windows
call :Environment
if !errorlevel! neq 0 exit /b 1

rem Tools.exe, the build's helper commands: compiled from Tools.cpp whenever it changes
set "TOOLS_SOURCE=%ROOT%\Framework\Automations\Tools.cpp"
set "TOOLS_BUILT=%ROOT%\Builds\Cache\Tools.cpp"
del /q "%ROOT%\Builds\Cache\Tools.*.old.exe" >nul 2>&1
set "TOOLS_CURRENT=FALSE"
if exist "%TOOLS%" if exist "%TOOLS_BUILT%" fc /b "%TOOLS_SOURCE%" "%TOOLS_BUILT%" >nul 2>&1 && set "TOOLS_CURRENT=TRUE"
if "%TOOLS_CURRENT%"=="FALSE" (
    echo "[Build] Building Tools.exe..."
    if exist "%TOOLS%" move /y "%TOOLS%" "%ROOT%\Builds\Cache\Tools.%RANDOM%.old.exe" >nul
    if not exist "%ROOT%\Builds\Cache" mkdir "%ROOT%\Builds\Cache"
    set "TOOLS_TARGET="
    if "%ENV_PLATFORM%"=="x86" set "TOOLS_TARGET=-m32"
    clang-cl !TOOLS_TARGET! /O2 /W3 /MT /std:c++20 /D_CRT_SECURE_NO_WARNINGS "%TOOLS_SOURCE%" /Fe"%TOOLS%" /Fo"%ROOT%\Builds\Cache\Tools.obj"
    if !errorlevel! neq 0 (
        echo "[Build][Error] Could not build Tools.exe."
        exit /b 1
    )
    del /q "%ROOT%\Builds\Cache\Tools.obj" >nul 2>&1
    copy /y "%TOOLS_SOURCE%" "%TOOLS_BUILT%" >nul
)

rem The generated headers in Framework\Includes, and the project's files and platforms
echo [Build 2%%] Refreshing include headers...
"%TOOLS%" Includes "%ROOT%" --project
if !errorlevel! neq 0 exit /b 1

rem The platform's own build
call "%PLATFORM_DIR%\Build.bat"
if !errorlevel! neq 0 (
    echo "[Build][Error] Build failed."
    exit /b 1
)

echo [Build 100%%] %CONFIG% build complete: %BUILD_PATH%
rem The watcher: refreshes the generated headers while Visual Studio is open
"%TOOLS%" Watch "%ROOT%"
exit /b 0

:MatchArchitecture
if defined TARGET exit /b 0
set "PLATFORM_ARCHITECTURES="
call "%~1\Config.bat"
if not defined PLATFORM_ARCHITECTURES exit /b 0
for %%A in (%PLATFORM_ARCHITECTURES%) do (
    if /i "%%A"=="%PLATFORM%" (
        set "TARGET=%~1"
        set "ARCHITECTURE=%%A"
    )
)
exit /b 0

:Environment
set "ENV_CACHE=%ROOT%\Builds\Cache\Windows\%ENV_PLATFORM%\Environment.txt"

if /i "%CACHE_ENVIRONMENT%"=="FALSE" goto :FindVisualStudio
if not exist "%ENV_CACHE%" goto :FindVisualStudio
for /f "usebackq delims=" %%L in ("%ENV_CACHE%") do set "%%L"
if not defined VCToolsInstallDir goto :FindVisualStudio
if not exist "%VCToolsInstallDir%" goto :FindVisualStudio
if not exist "%CLANG_CL%" goto :FindVisualStudio
if defined DXC if not exist "%DXC%" goto :FindVisualStudio
exit /b 0

:FindVisualStudio
set "VCVARSALL="
for %%X in (vcvarsall.bat) do set "VCVARSALL=%%~$PATH:X"
if defined VCVARSALL goto :Toolchain
set "VS_PATH="
set "VS_INSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VS_INSTALLER%\vswhere.exe" goto :MissingVisualStudio
pushd "%VS_INSTALLER%"
for /f "delims=" %%P in ('vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul') do set "VS_PATH=%%P"
popd
if not defined VS_PATH goto :MissingVisualStudio
set "VCVARSALL=%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARSALL%" goto :MissingVisualStudio

:Toolchain
call "%VCVARSALL%" %ENV_PLATFORM% >nul 2>&1
if not defined VCToolsInstallDir goto :MissingVisualStudio

set "CLANG_CL="
for %%X in (clang-cl.exe) do set "CLANG_CL=%%~$PATH:X"
if not defined CLANG_CL if exist "%VCINSTALLDIR%Tools\Llvm\x64\bin\clang-cl.exe" set "CLANG_CL=%VCINSTALLDIR%Tools\Llvm\x64\bin\clang-cl.exe"
if not defined CLANG_CL if exist "%ProgramFiles%\LLVM\bin\clang-cl.exe" set "CLANG_CL=%ProgramFiles%\LLVM\bin\clang-cl.exe"
if not defined CLANG_CL (
    echo "[Build][Error] clang-cl was not found. Install the 'C++ Clang Compiler for Windows' component in the Visual Studio Installer."
    call :OfferInstall
    exit /b 1
)
for %%D in ("%CLANG_CL%") do set "PATH=%%~dpD;%PATH%"

set "DXC="
if defined WindowsSdkVerBinPath if exist "%WindowsSdkVerBinPath%x64\dxc.exe" set "DXC=%WindowsSdkVerBinPath%x64\dxc.exe"
if not defined DXC for %%X in (dxc.exe) do set "DXC=%%~$PATH:X"

if /i "%CACHE_ENVIRONMENT%"=="FALSE" exit /b 0
if not exist "%ROOT%\Builds\Cache\Windows\%ENV_PLATFORM%" mkdir "%ROOT%\Builds\Cache\Windows\%ENV_PLATFORM%"
> "%ENV_CACHE%" set INCLUDE
for %%V in (LIB PATH VCToolsInstallDir VCINSTALLDIR VSINSTALLDIR WindowsSdkVerBinPath CLANG_CL DXC) do >> "%ENV_CACHE%" set %%V 2>nul
exit /b 0

:InstallScriptTemplate
set "SCRIPT_TEMPLATE=%ROOT%\Framework\Automations\Script.zip"
if not exist "%SCRIPT_TEMPLATE%" exit /b 0
powershell -NoProfile -ExecutionPolicy Bypass -Command "$documents = [Environment]::GetFolderPath('MyDocuments'); $installs = @(Get-ChildItem -LiteralPath $documents -Directory -Filter 'Visual Studio *' | Where-Object { Test-Path (Join-Path $_.FullName 'Templates') }); foreach ($install in $installs) { $items = Join-Path $install.FullName 'Templates\ItemTemplates'; New-Item -ItemType Directory -Force $items | Out-Null; Copy-Item -LiteralPath $env:SCRIPT_TEMPLATE -Destination $items -Force }; if ($installs.Count -eq 0) { exit 1 }"
if errorlevel 1 exit /b 0
if not exist "%ROOT%\Builds\Cache" mkdir "%ROOT%\Builds\Cache"
type nul > "%ROOT%\Builds\Cache\ScriptTemplate.installed"
echo "[Build] Installed the .hpp Template item template (Add > New Item). Restart Visual Studio once to see it."
exit /b 0

:MissingVisualStudio
echo "[Build][Error] Visual Studio with the C++ workload was not found."
call :OfferInstall
exit /b 1

:OfferInstall
set "VS_INSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VS_INSTALLER%\setup.exe" exit /b 0
set "INSTALL_PATH=%VS_PATH%"
pushd "%VS_INSTALLER%"
if not defined INSTALL_PATH for /f "delims=" %%P in ('vswhere.exe -latest -products * -property installationPath 2^>nul') do set "INSTALL_PATH=%%P"
popd
if not defined INSTALL_PATH exit /b 0
echo "[Build] Opening the Visual Studio Installer with what this project needs selected. Install it, then build again."
start "" "%VS_INSTALLER%\setup.exe" modify --installPath "%INSTALL_PATH%" --config "%ROOT%\.vsconfig"
exit /b 0
