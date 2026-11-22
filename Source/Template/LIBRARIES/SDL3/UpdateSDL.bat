@echo off
setlocal

rem | Builds SDL from its source for every platform, with "Build.bat sdl" in each platform's folder (Framework\Automations\
rem | Platforms), then replaces include and LICENSE.txt. Platforms whose tools are missing are skipped.
rem |   UpdateSDL.bat [source folder or .zip, or --download] [platform names] [--no-pause]
rem | --download takes the SDL release that matches include\SDL3.

set "SDL_DIR=%~dp0"
set "SDL_DIR=%SDL_DIR:~0,-1%"
for %%R in ("%SDL_DIR%\..\..") do set "ROOT=%%~fR"
set "WORK=%TEMP%\SDLUpdate_%RANDOM%%RANDOM%"
set "PAUSE=pause"
set "INPUT="
set "PLATFORMS="
:Arguments
if "%~1"=="" goto :ArgumentsDone
if /i "%~1"=="--no-pause" (set "PAUSE=") else if not defined INPUT (set "INPUT=%~1") else (set "PLATFORMS=%PLATFORMS% %~1")
shift
goto :Arguments
:ArgumentsDone
if not defined PLATFORMS (
    pushd "%ROOT%\Framework\Automations\Platforms"
    for /d %%P in (*) do findstr /b /i /c:":sdl" "%%P\Build.bat" >nul 2>&1 && call set "PLATFORMS=%%PLATFORMS%% %%P"
    popd
)

if defined INPUT goto :HaveInput
echo Drag SDL's source folder or .zip onto this window, then press Enter.
echo (A release's SDL3-x.y.z.zip from github.com/libsdl-org/SDL/releases, or leave it empty to download the one
echo that matches Libraries\SDL3\include.)
set /p "INPUT=Source: "
if not defined INPUT set "INPUT=--download"
:HaveInput
set "INPUT=%INPUT:"=%"
mkdir "%WORK%\Unpacked"

if /i not "%INPUT%"=="--download" goto :CheckInput
for /f "tokens=3" %%V in ('findstr /c:"#define SDL_MAJOR_VERSION" "%SDL_DIR%\include\SDL3\SDL_version.h"') do set "MAJOR=%%V"
for /f "tokens=3" %%V in ('findstr /c:"#define SDL_MINOR_VERSION" "%SDL_DIR%\include\SDL3\SDL_version.h"') do set "MINOR=%%V"
for /f "tokens=3" %%V in ('findstr /c:"#define SDL_MICRO_VERSION" "%SDL_DIR%\include\SDL3\SDL_version.h"') do set "MICRO=%%V"
set "INPUT=%WORK%\SDL3-%MAJOR%.%MINOR%.%MICRO%.zip"
echo [Update SDL] Downloading SDL %MAJOR%.%MINOR%.%MICRO%...
powershell -NoProfile -Command "$ProgressPreference = 'SilentlyContinue'; Invoke-WebRequest -UseBasicParsing -Uri ('https://github.com/libsdl-org/SDL/releases/download/release-' + $env:MAJOR + '.' + $env:MINOR + '.' + $env:MICRO + '/SDL3-' + $env:MAJOR + '.' + $env:MINOR + '.' + $env:MICRO + '.zip') -OutFile $env:INPUT"
if errorlevel 1 goto :Failed

:CheckInput
if not exist "%INPUT%" goto :NoInput
echo [Update SDL] Copying the source...
if /i "%INPUT:~-4%"==".zip" goto :Unzip
robocopy "%INPUT%" "%WORK%\Unpacked\Source" /E /NFL /NDL /NJH /NJS /NP /XD .git >nul
if errorlevel 8 goto :Failed
goto :FindSource

:Unzip
tar -xf "%INPUT%" -C "%WORK%\Unpacked" || goto :Failed

:FindSource
set "SOURCE="
if exist "%WORK%\Unpacked\CMakeLists.txt" set "SOURCE=%WORK%\Unpacked"
pushd "%WORK%\Unpacked"
for /d %%D in (*) do if exist "%%D\CMakeLists.txt" if exist "%%D\include\SDL3\SDL.h" set "SOURCE=%WORK%\Unpacked\%%D"
popd
if not defined SOURCE goto :NotSource
if not exist "%SOURCE%\include\SDL3\SDL_version.h" goto :NotSource

for /f "tokens=3" %%V in ('findstr /c:"#define SDL_MAJOR_VERSION" "%SOURCE%\include\SDL3\SDL_version.h"') do set "MAJOR=%%V"
for /f "tokens=3" %%V in ('findstr /c:"#define SDL_MINOR_VERSION" "%SOURCE%\include\SDL3\SDL_version.h"') do set "MINOR=%%V"
for /f "tokens=3" %%V in ('findstr /c:"#define SDL_MICRO_VERSION" "%SOURCE%\include\SDL3\SDL_version.h"') do set "MICRO=%%V"
echo [Update SDL] Found SDL %MAJOR%.%MINOR%.%MICRO%.

echo [Update SDL] Turning off the dynamic API in the static lib...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = Join-Path $env:SOURCE 'src\dynapi\SDL_dynapi.h'; if (!(Test-Path $p)) { exit 1 }; $t = [IO.File]::ReadAllText($p); if ($t -match 'SDL_STATIC_LIB') { exit 0 }; $anchor = '#elif defined(DYNAPI_NEEDS_DLOPEN)'; if (!$t.Contains($anchor)) { exit 1 }; $t = $t.Replace($anchor, \"#elif defined(SDL_STATIC_LIB) // SDL Template: off in the static lib only`n#define SDL_DYNAMIC_API 0`n\" + $anchor); [IO.File]::WriteAllText($p, $t)"
if errorlevel 1 echo [Update SDL][Warning] SDL_dynapi.h changed shape, so the static lib keeps the dynamic API. Everything still works; the Release exe is just a little bigger.

call :FindTools
set "UPDATED="
set "SKIPPED="
for %%P in (%PLATFORMS%) do (
    findstr /b /i /c:":sdl" "%ROOT%\Framework\Automations\Platforms\%%P\Build.bat" >nul 2>&1
    if errorlevel 1 (
        echo [Update SDL][Warning] %%P's Build.bat has no sdl part, in Framework\Automations\Platforms\%%P.
    ) else (
        call "%ROOT%\Framework\Automations\Platforms\%%P\Build.bat" sdl
        if errorlevel 2 goto :Failed
        if errorlevel 1 (call set "SKIPPED=%%SKIPPED%% %%P") else (call set "UPDATED=%%UPDATED%% %%P")
    )
)
if not defined UPDATED goto :Failed

echo [Update SDL] Replacing include and LICENSE.txt...
if exist "%SDL_DIR%\include\SDL3" rmdir /s /q "%SDL_DIR%\include\SDL3"
robocopy "%SOURCE%\include\SDL3" "%SDL_DIR%\include\SDL3" /E /XF SDL_test*.h /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto :Failed
if exist "%SOURCE%\LICENSE.txt" copy /Y "%SOURCE%\LICENSE.txt" "%SDL_DIR%\LICENSE.txt" >nul
rem New headers keep their old dates: clear every compiled cache, so nothing stale is reused
if exist "%ROOT%\Builds\Cache" for /d /r "%ROOT%\Builds\Cache" %%O in (Objects) do if exist "%%O" rmdir /s /q "%%O"

rmdir /s /q "%WORK%"
echo.
echo [Update SDL] Done: SDL %MAJOR%.%MINOR%.%MICRO% for%UPDATED%.
if defined SKIPPED echo [Update SDL] Skipped:%SKIPPED%. Their lib folders still hold the SDL they had.
%PAUSE%
exit /b 0

:FindTools
rem Visual Studio for Windows, and cmake and ninja (Visual Studio's, or the Android SDK's) for the other platforms
set "VS_PATH="
set "VS_INSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if exist "%VS_INSTALLER%\vswhere.exe" for /f "delims=" %%P in ('call "%VS_INSTALLER%\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul') do set "VS_PATH=%%P"
set "VCVARSALL=%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat"
set "CMAKE_TOOL="
set "NINJA_TOOL="
for %%X in (cmake.exe) do if not "%%~$PATH:X"=="" set "CMAKE_TOOL=%%~$PATH:X"
for %%X in (ninja.exe) do if not "%%~$PATH:X"=="" set "NINJA_TOOL=%%~$PATH:X"
set "VS_CMAKE=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake"
if not defined CMAKE_TOOL if exist "%VS_CMAKE%\CMake\bin\cmake.exe" set "CMAKE_TOOL=%VS_CMAKE%\CMake\bin\cmake.exe"
if not defined NINJA_TOOL if exist "%VS_CMAKE%\Ninja\ninja.exe" set "NINJA_TOOL=%VS_CMAKE%\Ninja\ninja.exe"
set "ANDROID_CMAKE="
for %%S in ("%TEMPLATE_TOOLS%\Android\Sdk") do if exist "%%~S\cmake" for /f "delims=" %%C in ('dir /b /ad /o:n "%%~S\cmake" 2^>nul') do set "ANDROID_CMAKE=%%~S\cmake\%%C\bin"
if not defined CMAKE_TOOL if exist "%ANDROID_CMAKE%\cmake.exe" set "CMAKE_TOOL=%ANDROID_CMAKE%\cmake.exe"
if not defined NINJA_TOOL if exist "%ANDROID_CMAKE%\ninja.exe" set "NINJA_TOOL=%ANDROID_CMAKE%\ninja.exe"
if not defined CMAKE_TOOL set "NINJA_TOOL="
exit /b 0

:NoInput
echo [Update SDL][Error] "%INPUT%" was not found.
goto :Cleanup

:NotSource
echo [Update SDL][Error] That is not SDL's source: no CMakeLists.txt next to include\SDL3. The SDL3-devel-*-VC.zip release is prebuilt, not source; use SDL3-x.y.z.zip or GitHub's Download ZIP.
goto :Cleanup

:Failed
echo [Update SDL][Error] Stopped, see the errors above.

:Cleanup
if exist "%WORK%" rmdir /s /q "%WORK%"
%PAUSE%
exit /b 1
