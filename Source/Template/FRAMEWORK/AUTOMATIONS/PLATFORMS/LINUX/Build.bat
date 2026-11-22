@echo off
setlocal enabledelayedexpansion
rem | Parts: "Build.bat run" (F5) runs the build in WSL, "Build.bat setup" installs zig and WSL,
rem | "Build.bat sdl" builds lib\Linux\x64 in WSL for UpdateSDL.bat.
rem TEMPLATE_TOOLS, the folder every tool is installed in: read from Windows, since Visual Studio keeps the value it started with
set "TEMPLATE_TOOLS="
for /f "tokens=2,*" %%A in ('reg query HKCU\Environment /v TEMPLATE_TOOLS 2^>nul ^| find "REG_"') do set "TEMPLATE_TOOLS=%%B"
for %%M in (run setup sdl) do if /i "%~1"=="%%M" goto :%%M

rem zig cross-compiles for x64 Linux with glibc 2.31 (2020), so the game runs on older distributions too
set "LINUX_TARGET=x86_64-linux-gnu.2.31"
set "LINUX_SDL=%SDL_LIB_DIR%\x64"
set "SHADERS_PATH=%ROOT%\Builds\Cache\Assets\Linux"

set "ZIG="
if exist "%TEMPLATE_TOOLS%\zig\zig.exe" set "ZIG=%TEMPLATE_TOOLS%\zig\zig.exe"
set "ZIG_GLOBAL_CACHE_DIR=%TEMPLATE_TOOLS%\zig\cache"
if not defined ZIG goto :OpenSetup
for /f "delims=" %%V in ('call "%ZIG%" version') do "%TOOLS%" Version "%PLATFORM_DIR%\Config.bat" ZIG_VERSION "%%V" && goto :OpenSetup
if not exist "%LINUX_SDL%\libSDL3.a" goto :OpenSetup

rem HLSL shaders compile to SPIR-V with the DXC the setup installs
set "SPIRV_DXC="
if exist "%TEMPLATE_TOOLS%\dxc\dxc.exe" set "SPIRV_DXC=%TEMPLATE_TOOLS%\dxc\dxc.exe"
set "HAS_SHADERS="
dir /s /b "%ROOT%\Program\Assets\*.hlsl" >nul 2>&1
if not errorlevel 1 set "HAS_SHADERS=TRUE"
if defined HAS_SHADERS if not defined SPIRV_DXC goto :OpenSetup

echo [Build 5%%] Preparing build folder...
if exist "%BUILD_PATH%" rmdir /s /q "%BUILD_PATH%"
mkdir "%BUILD_PATH%"
rem The program and Assets.data are made in Builds\Cache, where F5 runs them: the output folder holds only the packages
set "STAGE=%ROOT%\Builds\Cache\Linux\Game"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"

echo [Build 20%%] Compiling shaders...
if defined HAS_SHADERS (
    "%TOOLS%" Shaders "%ROOT%\Program\Assets" "%SHADERS_PATH%" "%SHADERS_PATH%\Shaders.state" --format .spv "%SPIRV_DXC%" -spirv -fspv-target-env=vulkan1.0 -T {profile} -E main {include} -O3 -Fo {output} {input}
    if errorlevel 1 exit /b 1
)

echo [Build 25%%] Baking assets...
set "PACK=%STAGE%\Assets.data"
set "ENCRYPT="
if /i "%ENABLE_ENCRYPTION%"=="TRUE" set "ENCRYPT=--encrypt"
if /i "%EMBED_ASSETS%"=="TRUE" set "PACK="
"%TOOLS%" Bake "%OBJECTS_PATH%\BakedAssets.hpp" "!PACK!" "%ROOT%\Program\Assets" "%SHADERS_PATH%" %ENCRYPT%
if errorlevel 1 exit /b 1

rem Tools Compile --gnu: GCC-style flags, no precompiled header
echo [Build 50%%] Compiling Release build...
"%TOOLS%" Compile "%ROOT%" "%OBJECTS_PATH%" "%ROOT%\Framework\Main.cpp" --pch Framework/Precompiled.hpp --gnu -- "%ZIG%" c++ -target %LINUX_TARGET% -std=c++20 -Os -g -DNDEBUG -fexceptions -ffunction-sections -fdata-sections -I"%ROOT%" -I"%OBJECTS_PATH%" -isystem "%ROOT%\Libraries\SDL3\include" -isystem "%ROOT%\Libraries" -Wno-pragma-once-outside-header
if errorlevel 1 exit /b 1

rem SDL and the C++ runtime linked in, unused code dropped: unstripped into Symbols\Linux\<version>, stripped to ship
echo [Build 70%%] Linking...
set "SYMBOLS=%ROOT%\Symbols\Linux\%PROGRAM_VERSION%"
if not exist "%SYMBOLS%" mkdir "%SYMBOLS%"
set "LINK_ARGS=-target %LINUX_TARGET% @"%OBJECTS_PATH%\Objects.rsp" -L"%LINUX_SDL%" -L"%ROOT%\Libraries" -lSDL3 -lm -lpthread -ldl -lrt -Wl,--gc-sections"
pushd "%STAGE%"
"%ZIG%" c++ %LINK_ARGS% -o "%SYMBOLS%\%PROGRAM_NAME%" && "%ZIG%" c++ %LINK_ARGS% -s -o "%PROGRAM_NAME%"
set "LINK_RESULT=!errorlevel!"
popd
if not "%LINK_RESULT%"=="0" exit /b %LINK_RESULT%

if /i "%COMPRESS_WITH_UPX%"=="TRUE" (
    echo [Build 80%%] Compressing the program with UPX...
    "%ROOT%\Libraries\UPX\upx.exe" --ultra-brute --lzma "%STAGE%\%PROGRAM_NAME%"
)

rem The package EXPORT_FORMAT asks for: the .tar.gz (it keeps the program runnable, which a .zip from Windows loses), or the AppImage
if /i not "%EXPORT_FORMAT%"=="APPIMAGE" (
    echo [Build 85%%] Packaging %PROGRAM_NAME%.tar.gz...
    call :TarGz
    exit /b !errorlevel!
)
echo [Build 85%%] Packaging %PROGRAM_NAME%.AppImage in WSL...
wsl -e true >nul 2>&1 || (
    echo "[Build][Error] The AppImage is made in WSL. Run "Framework\Automations\Platforms\Linux\Build.bat setup" to install it."
    exit /b 1
)
for /f "delims=" %%P in ('wsl wslpath -a "%BUILD_PATH:\=/%"') do set "WSL_BUILD=%%P"
for /f "delims=" %%P in ('wsl wslpath -a "%STAGE:\=/%"') do set "WSL_STAGE=%%P"
for /f "delims=" %%P in ('wsl wslpath -a "%ROOT:\=/%/Program/Assets/Icon.png"') do set "WSL_ICON=%%P"
for /f "delims=" %%P in ('wsl wslpath -a "%~dp0Linux.sh"') do set "WSL_SCRIPT=%%P"
wsl bash "!WSL_SCRIPT!" appimage "!WSL_STAGE!" "%PROGRAM_NAME%" "!WSL_ICON!" "!WSL_BUILD!/%PROGRAM_NAME%.AppImage"
exit /b %errorlevel%

rem Windows' own tar packs the list (mtree), which sets each file's Linux permissions
:TarGz
set "LIST=%OBJECTS_PATH%\Package.mtree"
set "TOP=%PROGRAM_NAME: =\040%"
> "%LIST%" echo #mtree
>> "%LIST%" echo !TOP! type=dir mode=0755
for /r "%STAGE%" %%F in (*) do (
    set "FILE=%%F"
    set "NAME=!FILE:%STAGE%\=!"
    set "MODE=0644"
    if /i "!NAME!"=="%PROGRAM_NAME%" set "MODE=0755"
    set "NAME=!NAME:\=/!"
    set "FILE=!FILE:\=/!"
    >> "%LIST%" echo !TOP!/!NAME: =\040! type=file mode=!MODE! contents=!FILE: =\040!
)
"%SystemRoot%\System32\tar.exe" -czf "%BUILD_PATH%\%PROGRAM_NAME%.tar.gz" "@%LIST%"
if errorlevel 1 (
    echo "[Build][Error] Packaging failed: Windows' tar (Windows 10 1803 or later) makes the .tar.gz."
    exit /b 1
)
exit /b 0

:OpenSetup
echo "[Build][Error] Linux builds need zig, SDL for Linux, and DXC for the shaders. Linux's setup opened in a new window: build again when it is done."
start "Linux setup" cmd /c ""%~f0" setup"
exit /b 1

:setup
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup.ps1"
pause
exit /b 0

:run
for %%R in ("%~dp0..\..\..\..") do set "ROOT=%%~fR"
rem Out of the build folder, so nothing started here keeps it in use
cd /d "%TEMP%"
call "%ROOT%\Config.bat"
set "GAME=%ROOT%\Builds\Cache\Linux\Game"
if not exist "%GAME%\%PROGRAM_NAME%" (
    echo Build the Linux configuration first.
    exit /b 1
)
wsl -e true >nul 2>&1 || (
    echo WSL cannot run Linux programs yet, so the Linux setup starts now.
    call "%~f0" setup
    wsl -e true >nul 2>&1 || exit /b 1
)
for /f "delims=" %%P in ('wsl wslpath -a "%GAME:\=/%"') do set "WSL_GAME=%%P"
for /f "delims=" %%P in ('wsl wslpath -a "%~dp0Linux.sh"') do set "WSL_SCRIPT=%%P"
wsl bash "%WSL_SCRIPT%" run "%WSL_GAME%" "%PROGRAM_NAME%"
exit /b %errorlevel%

:sdl
rem SDL built in WSL by Linux.sh. Exit code 0 updated, 1 skipped, 2 failed
where wsl >nul 2>&1 || goto :NoWsl
set "WSL_SOURCE=?"
for /f "delims=" %%P in ('wsl wslpath -a "%SOURCE:\=/%" 2^>nul') do set "WSL_SOURCE=%%P"
for /f "delims=" %%P in ('wsl wslpath -a "%~dp0Linux.sh" 2^>nul') do set "WSL_SCRIPT=%%P"
for /f "delims=" %%P in ('wsl wslpath -a "%SDL_DIR%\lib\Linux\x64" 2^>nul') do set "WSL_OUTPUT=%%P"
if not "%WSL_SOURCE:~0,1%"=="/" goto :NoWsl
echo [Update SDL] Building libSDL3.a for Linux in WSL...
wsl bash "%WSL_SCRIPT%" sdl "%WSL_SOURCE%" "%WSL_OUTPUT%" && exit /b 0
echo [Update SDL][Warning] Linux skipped: building SDL failed in WSL, see above.
exit /b 1

:NoWsl
echo [Update SDL][Warning] Linux skipped: it builds inside WSL. Run "Framework\Automations\Platforms\Linux\Build.bat setup" to install it.
exit /b 1
