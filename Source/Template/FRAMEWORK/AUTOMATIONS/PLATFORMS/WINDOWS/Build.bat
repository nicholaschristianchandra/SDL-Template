@echo off
setlocal enabledelayedexpansion
rem | Debug: a host exe plus Program.dll, which the running game swaps in (hot reload). Release: one exe.
rem | Parts: "Build.bat sdl" builds lib\Windows for UpdateSDL.bat.
if /i "%~1"=="sdl" goto :SDL

rem Compiler and linker flags shared by Debug and Release
if "%ARCHITECTURE%"=="x86" (set "CLANG_TARGET=-m32") else (set "CLANG_TARGET=")
set "FUSE_LD=-fuse-ld=link"
if /i not "%USE_LLD%"=="FALSE" for %%X in (lld-link.exe) do if not "%%~$PATH:X"=="" set "FUSE_LD=-fuse-ld=lld"
set "SYSTEM_LIBRARIES=kernel32.lib user32.lib gdi32.lib winmm.lib imm32.lib ole32.lib oleaut32.lib version.lib uuid.lib advapi32.lib setupapi.lib shell32.lib dinput8.lib"
set "INCLUDE_FLAGS=/I"%ROOT%" /external:I"%ROOT%\Libraries\SDL3\include" /external:I"%ROOT%\Libraries""
set "GAME_PATH=%BUILD_PATH%\%PROGRAM_NAME%.exe"

rem A running game: Debug hot reloads it, Release closes it first
set "GAME_RUNNING=FALSE"
if not exist "%GAME_PATH%" goto :GameChecked
2>nul ( >>"%GAME_PATH%" (call ) ) || set "GAME_RUNNING=TRUE"
:GameChecked

if "%GAME_RUNNING%"=="TRUE" if "%CONFIG%"=="Debug" (
    echo "[Build] %PROGRAM_NAME%.exe is running. Hot reloading it..."
    call :BuildProgram
    exit /b !errorlevel!
)
if "%GAME_RUNNING%"=="TRUE" (
    echo "[Build] %PROGRAM_NAME%.exe is running. Closing it..."
    powershell -NoProfile -Command "$path = [System.IO.Path]::GetFullPath($env:GAME_PATH); $running = Get-Process | Where-Object { $_.Path -eq $path }; $running | Stop-Process -Force; $running | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue"
)

echo [Build 5%%] Preparing build folder...
if exist "%BUILD_PATH%" rmdir /s /q "%BUILD_PATH%"
mkdir "%BUILD_PATH%"
if not exist "%OBJECTS_PATH%" mkdir "%OBJECTS_PATH%"

rem The exe's icon, every size from 16 to 256, made from Program\Assets\Icon.png
set "ICON_LINE="
if exist "%ROOT%\Program\Assets\Icon.png" (
    "%TOOLS%" Icon "%ROOT%\Program\Assets\Icon.png" "%OBJECTS_PATH%\Icon.ico"
    if errorlevel 1 exit /b 1
    set "ICON_LINE=IDI_ICON1 ICON "Icon.ico""
)

if "%CONFIG%"=="Debug" goto :Debug

echo [Build 18%%] Checking SDL3...
if not exist "%SDL_LIB_DIR%\SDL3-static.lib" (
    echo "[Build][Error] SDL3-static.lib is missing from %SDL_LIB_DIR%. Run Libraries\SDL3\UpdateSDL.bat."
    exit /b 1
)

echo [Build 20%%] Compiling shaders...
"%TOOLS%" Shaders "%ROOT%\Program\Assets" "%ROOT%\Builds\Cache\Assets\Release" "%ROOT%\Builds\Cache\Assets\Release\Shaders.state" --format .dxil "%DXC%" -T {profile} -E main {include} -O3 -Fo {output} {input}
if errorlevel 1 exit /b 1

echo [Build 25%%] Baking assets...
set "PACK=%BUILD_PATH%\Assets.data"
set "ENCRYPT="
if /i "%ENABLE_ENCRYPTION%"=="TRUE" set "ENCRYPT=--encrypt"
if /i "%EMBED_ASSETS%"=="TRUE" set "PACK="
"%TOOLS%" Bake "%OBJECTS_PATH%\BakedAssets.hpp" "!PACK!" "%ROOT%\Program\Assets" "%ROOT%\Builds\Cache\Assets\Release" %ENCRYPT%
if errorlevel 1 exit /b 1

echo [Build 30%%] Compiling resources...
call :WriteResources > "%OBJECTS_PATH%\Resources.rc"
rc /nologo /i "%OBJECTS_PATH%" /fo "%OBJECTS_PATH%\Resources.res" "%OBJECTS_PATH%\Resources.rc"
if errorlevel 1 exit /b 1

rem Tools Compile: only the changed units, with the precompiled header
echo [Build 50%%] Compiling Release build...
"%TOOLS%" Compile "%ROOT%" "%OBJECTS_PATH%" "%ROOT%\Framework\Main.cpp" --pch Framework/Precompiled.hpp -- clang-cl %CLANG_TARGET% /D_CRT_SECURE_NO_WARNINGS /DNDEBUG /Z7 /MT /std:c++20 /EHsc /Gy /Gw /clang:-Oz /W3 -mllvm -trap-unreachable %INCLUDE_FLAGS% /I"%OBJECTS_PATH%" -Wno-pragma-once-outside-header
if errorlevel 1 exit /b 1

set "SUBSYSTEM=WINDOWS"
if /i "%CONSOLE_PROGRAM%"=="TRUE" set "SUBSYSTEM=CONSOLE"

rem Static SDL and C runtime: one exe that needs nothing installed
echo [Build 70%%] Linking...
pushd "%BUILD_PATH%"
set "ROOT_RELATIVE=..\..\..\..\.."
link.exe /nologo @"%OBJECTS_PATH%\Objects.rsp" "%OBJECTS_PATH%\Resources.res" /OUT:"%PROGRAM_NAME%.exe" /DEBUG /PDB:"%OBJECTS_PATH%\%PROGRAM_NAME%.pdb" /INCREMENTAL:NO /MANIFEST:NO /OPT:REF /OPT:ICF /SUBSYSTEM:%SUBSYSTEM% /ENTRY:mainCRTStartup /NODEFAULTLIB:libucrt.lib ucrt.lib /NODEFAULTLIB:msvcrt.lib /IGNORE:4217,4286 /LIBPATH:"%ROOT_RELATIVE%\Libraries\SDL3\lib\Windows\%ARCHITECTURE%" /LIBPATH:"%ROOT_RELATIVE%\Libraries" SDL3-static.lib %SYSTEM_LIBRARIES%
set "LINK_RESULT=!errorlevel!"
popd
if not "!LINK_RESULT!"=="0" exit /b 1

if /i not "%EMBED_MANIFEST%"=="TRUE" goto :SkipManifest
if not exist "%PLATFORM_DIR%\Manifest.manifest" goto :SkipManifest
echo [Build 85%%] Embedding manifest...
set "MANIFEST_TEMP=%OBJECTS_PATH%\Manifest.manifest"
(
    for /f "usebackq delims=" %%L in ("%PLATFORM_DIR%\Manifest.manifest") do (
        set "LINE=%%L"
        echo !LINE:YourProjectName=%PROGRAM_NAME: =%!
    )
) > "!MANIFEST_TEMP!"
mt -nologo -manifest "!MANIFEST_TEMP!" -outputresource:"%GAME_PATH%";1
if errorlevel 1 exit /b 1
:SkipManifest

if /i "%COMPRESS_WITH_UPX%"=="TRUE" (
    echo [Build 95%%] Compressing executable with UPX...
    "%ROOT%\Libraries\UPX\upx.exe" --ultra-brute --lzma "%GAME_PATH%"
)

rem Symbols\: this exe and its .pdb, kept to read its crash reports
"%TOOLS%" Symbols "%GAME_PATH%" "%OBJECTS_PATH%\%PROGRAM_NAME%.pdb" "%ROOT%\Symbols"
exit /b %errorlevel%

:Debug
rem Debug: SDL3.dll, a host exe, and the game in Program.dll
for %%F in (SDL3.dll SDL3.lib) do (
    if not exist "%SDL_LIB_DIR%\%%F" (
        echo "[Build][Error] %%F is missing from %SDL_LIB_DIR%. Run Libraries\SDL3\UpdateSDL.bat."
        exit /b 1
    )
)

echo [Build 10%%] Compiling the icon...
if defined ICON_LINE (> "%OBJECTS_PATH%\Icon.rc" echo !ICON_LINE!) else (type nul > "%OBJECTS_PATH%\Icon.rc")
rc /nologo /i "%OBJECTS_PATH%" /fo "%OBJECTS_PATH%\Icon.res" "%OBJECTS_PATH%\Icon.rc"

rem The host: Main.cpp only, loading and reloading Program.dll
echo [Build 20%%] Compiling the host...
clang-cl %FUSE_LD% %CLANG_TARGET% /D_CRT_SECURE_NO_WARNINGS /Zi /Od /MDd /DIS_DEBUG /std:c++20 /EHsc /W3 -mllvm -trap-unreachable %INCLUDE_FLAGS% "%ROOT%\Framework\Main.cpp" "%OBJECTS_PATH%\Icon.res" /Fe"%GAME_PATH%" /Fo"%OBJECTS_PATH%\\" /Fd"%OBJECTS_PATH%\%PROGRAM_NAME%.pdb" /link /SUBSYSTEM:CONSOLE /LIBPATH:"%SDL_LIB_DIR%" SDL3.lib /PDB:"%OBJECTS_PATH%\%PROGRAM_NAME%.pdb"
set "BUILD_RESULT=!errorlevel!"
copy /Y "%SDL_LIB_DIR%\SDL3.dll" "%BUILD_PATH%\" >nul
if not "!BUILD_RESULT!"=="0" exit /b 1

echo [Build 50%%] Building Program.dll...
call :BuildProgram
exit /b %errorlevel%

:BuildProgram
rem The game: shaders, then the scripts into Program.dll
if not exist "%BUILD_PATH%" (
    echo "[Build][Error] %BUILD_PATH% does not exist. Build Debug %ARCHITECTURE% first."
    exit /b 1
)

"%TOOLS%" Shaders "%ROOT%\Program\Assets" "%ROOT%\Builds\Cache\Assets\Debug" "%ROOT%\Builds\Cache\Assets\Debug\Shaders.state" --format .dxil "%DXC%" -T {profile} -E main {include} -Zi -Qembed_debug -Od -Fo {output} {input}
if !errorlevel! neq 0 exit /b 1

"%TOOLS%" Compile "%ROOT%" "%OBJECTS_PATH%" --pch Framework/Precompiled.hpp -- clang-cl %CLANG_TARGET% /D_CRT_SECURE_NO_WARNINGS /Zi /Od /MDd /DIS_DEBUG /std:c++20 /EHsc /W3 -mllvm -trap-unreachable %INCLUDE_FLAGS% -Wno-pragma-once-outside-header
if !errorlevel! neq 0 exit /b 1

rem Linked under another name, then moved over Program.dll, which the running game watches
pushd "%BUILD_PATH%"
del /q "%OBJECTS_PATH%\Program_*.pdb" >nul 2>&1
set "STAMP=%RANDOM%%RANDOM%"
clang-cl %FUSE_LD% %CLANG_TARGET% /LD @"%OBJECTS_PATH%\Objects.rsp" /Fe"Program_Temp.dll" /link /DEBUG /PDB:"%OBJECTS_PATH%\Program_%STAMP%.pdb" /IMPLIB:"%OBJECTS_PATH%\Program.lib" /LIBPATH:"%SDL_LIB_DIR%" /LIBPATH:"%ROOT%\Libraries" SDL3.lib %SYSTEM_LIBRARIES%
set "LINK_RESULT=!errorlevel!"
if !LINK_RESULT! equ 0 move /Y "Program_Temp.dll" "Program.dll" >nul
popd
if !LINK_RESULT! neq 0 exit /b 1
echo "[Build] Program.dll built."
exit /b 0

:WriteResources
rem The exe's icon and version details (Properties, Details)
for /f "tokens=1-4 delims=." %%A in ("%PROGRAM_VERSION%.0.0.0") do set "VERSION_NUMBERS=%%A,%%B,%%C,%%D"
if defined ICON_LINE echo !ICON_LINE!
echo 1 VERSIONINFO
echo FILEVERSION %VERSION_NUMBERS%
echo PRODUCTVERSION %VERSION_NUMBERS%
echo FILEOS 0x40004
echo FILETYPE 0x1
echo BEGIN
echo   BLOCK "StringFileInfo"
echo   BEGIN
echo     BLOCK "040904b0"
echo     BEGIN
echo       VALUE "FileDescription", "%PROGRAM_NAME%"
echo       VALUE "FileVersion", "%PROGRAM_VERSION%"
echo       VALUE "InternalName", "%PROGRAM_NAME%"
echo       VALUE "OriginalFilename", "%PROGRAM_NAME%.exe"
echo       VALUE "ProductName", "%PROGRAM_NAME%"
echo       VALUE "ProductVersion", "%PROGRAM_VERSION%"
echo     END
echo   END
echo   BLOCK "VarFileInfo"
echo   BEGIN
echo     VALUE "Translation", 0x409, 1200
echo   END
echo END
exit /b 0

:SDL
rem SDL3.dll and SDL3.lib for Debug, SDL3-static.lib for Release, for x64 and x86. Exit code 0 updated, 1 skipped, 2 failed.
if not exist "%VCVARSALL%" (
    echo [Update SDL][Warning] Windows skipped: Visual Studio with "Desktop development with C++" was not found.
    exit /b 1
)
call :BuildSdl x64 || exit /b 2
call :BuildSdl x86 || exit /b 2
for %%P in (x64 x86) do (
    if not exist "%SDL_DIR%\lib\Windows\%%P" mkdir "%SDL_DIR%\lib\Windows\%%P"
    copy /Y "%WORK%\Out\%%P\*" "%SDL_DIR%\lib\Windows\%%P\" >nul || exit /b 2
)
exit /b 0

:BuildSdl
setlocal
echo [Update SDL] Setting up Visual Studio for %1...
call "%VCVARSALL%" %1 >nul 2>&1
if not defined VCToolsInstallDir (
    echo [Update SDL][Error] vcvarsall.bat %1 failed.
    exit /b 1
)
if not defined CMAKE_TOOL (
    echo [Update SDL][Error] cmake was not found. Install "C++ CMake tools for Windows" with the Visual Studio Installer.
    exit /b 1
)
set "GENERATOR=NMake Makefiles"
if defined NINJA_TOOL set "GENERATOR=Ninja"
if defined NINJA_TOOL for %%D in ("%NINJA_TOOL%") do set "PATH=%%~dpD;%PATH%"
set "OUT=%WORK%\Out\%1"
set "BUILD_DIR=%WORK%\Build\Windows\%1"
mkdir "%OUT%"
set "FLAGS=-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=MinSizeRel "-DCMAKE_C_FLAGS_MINSIZEREL=/O1 /Ob1 /DNDEBUG /Gy /Gw" "-DCMAKE_CXX_FLAGS_MINSIZEREL=/O1 /Ob1 /DNDEBUG /Gy /Gw" -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF"

echo [Update SDL] Building SDL3.dll for %1...
"%CMAKE_TOOL%" -S "%SOURCE%" -B "%BUILD_DIR%\Shared" -G "%GENERATOR%" %FLAGS% "-DCMAKE_SHARED_LINKER_FLAGS_MINSIZEREL=/OPT:REF /OPT:ICF" -DSDL_SHARED=ON -DSDL_STATIC=OFF || exit /b 1
"%CMAKE_TOOL%" --build "%BUILD_DIR%\Shared" || exit /b 1
copy /Y "%BUILD_DIR%\Shared\SDL3.dll" "%OUT%\" >nul || exit /b 1
copy /Y "%BUILD_DIR%\Shared\SDL3.lib" "%OUT%\" >nul || exit /b 1

echo [Update SDL] Building SDL3-static.lib for %1...
"%CMAKE_TOOL%" -S "%SOURCE%" -B "%BUILD_DIR%\Static" -G "%GENERATOR%" %FLAGS% -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DSDL_SHARED=OFF -DSDL_STATIC=ON || exit /b 1
"%CMAKE_TOOL%" --build "%BUILD_DIR%\Static" || exit /b 1
copy /Y "%BUILD_DIR%\Static\SDL3-static.lib" "%OUT%\" >nul || exit /b 1
endlocal
exit /b 0
