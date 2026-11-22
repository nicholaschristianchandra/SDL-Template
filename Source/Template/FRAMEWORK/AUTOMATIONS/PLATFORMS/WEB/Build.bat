@echo off
setlocal enabledelayedexpansion
rem | Parts: "Build.bat run" (F5) serves the build and opens your browser, "Build.bat setup" installs Emscripten,
rem | "Build.bat sdl" builds lib\Web for UpdateSDL.bat.
rem TEMPLATE_TOOLS, the folder every tool is installed in: read from Windows, since Visual Studio keeps the value it started with
set "TEMPLATE_TOOLS="
for /f "tokens=2,*" %%A in ('reg query HKCU\Environment /v TEMPLATE_TOOLS 2^>nul ^| find "REG_"') do set "TEMPLATE_TOOLS=%%B"
for %%M in (run setup sdl) do if /i "%~1"=="%%M" goto :%%M

set "SHADERS_PATH=%ROOT%\Builds\Cache\Assets\Web"

call :FindEmscripten
if not defined EMCC_PYTHON goto :OpenSetup
rem Emscripten's helper programs (file_packager.exe) find Python through EMSDK_PYTHON
set "EMSDK_PYTHON=%EMCC_PYTHON%"
if not exist "%SDL_LIB_DIR%\libSDL3.a" goto :OpenSetup
rem SDL must come from the same Emscripten as the game: rebuilt when it changes
set "SDL_EMSCRIPTEN="
set "THIS_EMSCRIPTEN="
if exist "%SDL_LIB_DIR%\Emscripten.txt" set /p SDL_EMSCRIPTEN=<"%SDL_LIB_DIR%\Emscripten.txt"
for /f "delims=" %%V in ('call "%EMCC_PYTHON%" -E "%EMCC_PY%" -dumpversion 2^>nul') do set "THIS_EMSCRIPTEN=%%V"
"%TOOLS%" Version "%PLATFORM_DIR%\Config.bat" EMSCRIPTEN_VERSION "!THIS_EMSCRIPTEN!" && goto :OpenSetup
if not "!SDL_EMSCRIPTEN!"=="!THIS_EMSCRIPTEN!" (
    echo "[Build] SDL for the web was built with another Emscripten (!SDL_EMSCRIPTEN!): rebuilding it with !THIS_EMSCRIPTEN!..."
    call "%ROOT%\Libraries\SDL3\UpdateSDL.bat" --download Web --no-pause
    if errorlevel 1 exit /b 1
)

echo [Build 5%%] Preparing build folder...
if exist "%BUILD_PATH%" rmdir /s /q "%BUILD_PATH%"
mkdir "%BUILD_PATH%"
if not exist "%OBJECTS_PATH%" mkdir "%OBJECTS_PATH%"
rem Emscripten's temporary files, in the build cache instead of %TEMP%
set "EMCC_TEMP_DIR=%OBJECTS_PATH%"

dir /s /b "%ROOT%\Program\Assets\*.hlsl" >nul 2>&1
if not errorlevel 1 echo "[Build][Warning] Browsers have no SDL_GPU, so the project's shaders are left out of the web build."

echo [Build 25%%] Baking assets...
set "PACK=%BUILD_PATH%\Assets.data"
set "ENCRYPT="
if /i "%ENABLE_ENCRYPTION%"=="TRUE" set "ENCRYPT=--encrypt"
"%TOOLS%" Bake "%OBJECTS_PATH%\BakedAssets.hpp" "!PACK!" "%ROOT%\Program\Assets" "%SHADERS_PATH%" %ENCRYPT%
if errorlevel 1 exit /b 1

rem Tools Compile --gnu: emcc takes GCC-style flags, no precompiled header
echo [Build 50%%] Compiling Release build...
"%TOOLS%" Compile "%ROOT%" "%OBJECTS_PATH%" "%ROOT%\Framework\Main.cpp" --pch Framework/Precompiled.hpp --gnu -- "%EMCC_PYTHON%" -E "%EMCC_PY%" -std=c++20 -Oz -DNDEBUG -fwasm-exceptions -I"%ROOT%" -I"%OBJECTS_PATH%" -isystem "%ROOT%\Libraries\SDL3\include" -isystem "%ROOT%\Libraries" -Wno-pragma-once-outside-header
if errorlevel 1 exit /b 1

rem index.html, .js and .wasm, with Assets.data preloaded as index.data
echo [Build 70%%] Linking...
set "SHELL_PAGE=%OBJECTS_PATH%\Shell.html"
powershell -NoProfile -Command "$t = [IO.File]::ReadAllText($env:PLATFORM_DIR + '\Shell.html'); [IO.File]::WriteAllText($env:SHELL_PAGE, $t.Replace('{{{ PROGRAM_NAME }}}', [Net.WebUtility]::HtmlEncode($env:PROGRAM_NAME)))"
if errorlevel 1 exit /b 1
set "PRELOAD="
if exist "%BUILD_PATH%\Assets.data" set "PRELOAD=--preload-file Assets.data@/Assets.data"
pushd "%BUILD_PATH%"
"%EMCC_PYTHON%" -E "%EMCC_PY%" @"%OBJECTS_PATH%\Objects.rsp" "%SDL_LIB_DIR%\libSDL3.a" -L"%ROOT%\Libraries" -o index.html --shell-file "%SHELL_PAGE%" -Oz -fwasm-exceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=web -sMINIFY_HTML=0 %PRELOAD%
set "LINK_RESULT=!errorlevel!"
popd
if not "!LINK_RESULT!"=="0" exit /b 1
if defined PRELOAD del /q "%BUILD_PATH%\Assets.data"

if exist "%ROOT%\Program\Assets\Icon.png" for %%S in (32 180 192) do (
    "%TOOLS%" Icon "%ROOT%\Program\Assets\Icon.png" "%BUILD_PATH%\icon-%%S.png" %%S
    if errorlevel 1 exit /b 1
)
echo "[Build] Ctrl+F5 opens it in your browser. To publish it, upload every file in %BUILD_PATH% (itch.io: zip them as an HTML game)."
exit /b 0

:FindEmscripten
rem emcc.bat only runs Python on emcc.py: calling Python directly lets Tools.exe start it like any compiler
set "EMCC_PY="
set "EMCC_PYTHON="
if exist "%TEMPLATE_TOOLS%\emsdk\upstream\emscripten\emcc.py" set "EMCC_PY=%TEMPLATE_TOOLS%\emsdk\upstream\emscripten\emcc.py"
if not defined EMCC_PY exit /b 0
for %%E in ("%EMCC_PY%\..\..\..") do set "EMSDK_ROOT=%%~fE"
if defined EMSDK_PYTHON if exist "%EMSDK_PYTHON%" set "EMCC_PYTHON=%EMSDK_PYTHON%"
if not defined EMCC_PYTHON if exist "%EMSDK_ROOT%\python" (
    pushd "%EMSDK_ROOT%\python"
    for /d %%P in (*) do if exist "%%P\python.exe" set "EMCC_PYTHON=%EMSDK_ROOT%\python\%%P\python.exe"
    popd
)
exit /b 0

:OpenSetup
echo "[Build][Error] Web builds need Emscripten and SDL for the web. The web's setup opened in a new window: build again when it is done."
start "Web setup" cmd /c ""%~f0" setup"
exit /b 1

:setup
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup.ps1"
pause
exit /b 0

:run
for %%R in ("%~dp0..\..\..\..") do set "ROOT=%%~fR"
rem Out of the build folder, so nothing started here keeps it in use
cd /d "%TEMP%"
set "SITE=%ROOT%\Builds\Outputs\Web\Release"
if not exist "%SITE%\index.html" (
    echo Build the Web configuration first.
    exit /b 1
)
set "PYTHON="
if exist "%TEMPLATE_TOOLS%\emsdk\python" call :FindPython "%TEMPLATE_TOOLS%\emsdk\python"
if not defined PYTHON for %%X in (python.exe) do if not "%%~$PATH:X"=="" set "PYTHON=%%~$PATH:X"
if not defined PYTHON (
    echo Python was not found. Run "Framework\Automations\Platforms\Web\Build.bat setup".
    exit /b 1
)
rem A file server that stops the browser keeping old builds, on port 8000, or a free one when something holds it
"%PYTHON%" -c "import http.server as h, functools as f, socket, sys, webbrowser as w; C = type('C', (h.SimpleHTTPRequestHandler,), {'end_headers': lambda s: (s.send_header('Cache-Control', 'no-store'), h.SimpleHTTPRequestHandler.end_headers(s))}); S = type('S', (h.ThreadingHTTPServer,), {'allow_reuse_address': False}); t = socket.socket(); busy = t.connect_ex(('127.0.0.1', 8000)) == 0; t.close(); s = S(('127.0.0.1', 0 if busy else 8000), f.partial(C, directory=sys.argv[1])); u = 'http://localhost:%%d/' %% s.server_address[1]; print('Playing at ' + u + ' - close this window to stop.', flush=True); w.open(u); s.serve_forever()" "%SITE%"
exit /b 0

:FindPython
pushd "%~1"
for /d %%P in (*) do if exist "%%P\python.exe" set "PYTHON=%~1\%%P\python.exe"
popd
exit /b 0

:sdl
rem SDL built with Emscripten. Exit code 0 updated, 1 skipped, 2 failed
set "EMCC_TEMP_DIR=%WORK%"
set "EMSDK_ROOT="
if exist "%TEMPLATE_TOOLS%\emsdk\emsdk_env.bat" set "EMSDK_ROOT=%TEMPLATE_TOOLS%\emsdk"
if not defined EMSDK_ROOT (
    echo [Update SDL][Warning] Web skipped: Emscripten is not installed. Run "Framework\Automations\Platforms\Web\Build.bat setup".
    exit /b 1
)
if not defined NINJA_TOOL (
    echo [Update SDL][Warning] Web skipped: cmake and ninja were not found. Install "C++ CMake tools for Windows" with the Visual Studio Installer.
    exit /b 1
)
call "%EMSDK_ROOT%\emsdk_env.bat" >nul 2>&1
for %%D in ("%NINJA_TOOL%") do set "PATH=%%~dpD;%PATH%"
echo [Update SDL] Building libSDL3.a for Web...
call emcmake "%CMAKE_TOOL%" -S "%SOURCE%" -B "%WORK%\Build\Web" -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF || exit /b 2
"%CMAKE_TOOL%" --build "%WORK%\Build\Web" || exit /b 2
if not exist "%SDL_DIR%\lib\Web" mkdir "%SDL_DIR%\lib\Web"
copy /Y "%WORK%\Build\Web\libSDL3.a" "%SDL_DIR%\lib\Web\" >nul || exit /b 2
rem Build.bat rebuilds it when the game is built with another Emscripten
call emcc -dumpversion > "%SDL_DIR%\lib\Web\Emscripten.txt"
exit /b 0
