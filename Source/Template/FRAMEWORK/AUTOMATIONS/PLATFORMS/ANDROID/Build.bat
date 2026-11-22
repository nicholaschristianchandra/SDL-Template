@echo off
setlocal enabledelayedexpansion
rem | Built without Gradle: the NDK compiles the game, javac and d8 SDL's Java side, aapt2 and bundletool package.
rem | Parts: "Build.bat run" (F5) installs and starts it on a phone or the emulator, "Build.bat setup" installs Java,
rem | the SDK and NDK, "Build.bat keystore" makes the release key, "Build.bat sdl" builds lib\Android for UpdateSDL.bat.
rem TEMPLATE_TOOLS, the folder every tool is installed in: read from Windows, since Visual Studio keeps the value it started with
set "TEMPLATE_TOOLS="
for /f "tokens=2,*" %%A in ('reg query HKCU\Environment /v TEMPLATE_TOOLS 2^>nul ^| find "REG_"') do set "TEMPLATE_TOOLS=%%B"
set "ANDROID_AVD_HOME=%TEMPLATE_TOOLS%\Android\user\avd"
set "ANDROID_USER_HOME=%TEMPLATE_TOOLS%\Android\user"
for %%M in (run setup keystore sdl) do if /i "%~1"=="%%M" goto :%%M

set "SHADERS_PATH=%ROOT%\Builds\Cache\Assets\Android"
set "STAGE=%OBJECTS_PATH%\Package"
set "APK=%BUILD_PATH%\%PROGRAM_NAME%.apk"
set "AAB=%BUILD_PATH%\%PROGRAM_NAME%.aab"
if exist "%ROOT%\Keystore\Keystore.bat" call "%ROOT%\Keystore\Keystore.bat"
set "MAKE_APK=TRUE"
set "MAKE_AAB=TRUE"
if /i "%EXPORT_FORMAT%"=="APK" set "MAKE_AAB="
if /i "%EXPORT_FORMAT%"=="AAB" set "MAKE_APK="

call :FindTools
if errorlevel 1 goto :OpenSetup
if not exist "%SDL_LIB_DIR%\java\org\libsdl\app\SDLActivity.java" goto :OpenSetup
for %%A in (%ABIS%) do if not exist "%SDL_LIB_DIR%\%%A\libSDL3.so" goto :OpenSetup
if not exist "%ROOT%\Program\Assets\Icon.png" (
    echo "[Build][Error] Android apps need an icon: put one in Program\Assets\Icon.png."
    exit /b 1
)

rem HLSL shaders compile to SPIR-V with the DXC the setup installs
set "SPIRV_DXC="
if exist "%TEMPLATE_TOOLS%\dxc\dxc.exe" set "SPIRV_DXC=%TEMPLATE_TOOLS%\dxc\dxc.exe"
set "HAS_SHADERS="
dir /s /b "%ROOT%\Program\Assets\*.hlsl" >nul 2>&1
if not errorlevel 1 set "HAS_SHADERS=TRUE"
if defined HAS_SHADERS if not defined SPIRV_DXC goto :OpenSetup

echo [Build 5%%] Preparing build folder...
if exist "%BUILD_PATH%" rmdir /s /q "%BUILD_PATH%"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%BUILD_PATH%" "%STAGE%\assets" "%STAGE%\classes" "%STAGE%\dex" "%STAGE%\lib" "%STAGE%\res"

echo [Build 20%%] Compiling shaders...
if defined HAS_SHADERS (
    "%TOOLS%" Shaders "%ROOT%\Program\Assets" "%SHADERS_PATH%" "%SHADERS_PATH%\Shaders.state" --format .spv "%SPIRV_DXC%" -spirv -fspv-target-env=vulkan1.0 -T {profile} -E main {include} -O3 -Fo {output} {input}
    if errorlevel 1 exit /b 1
)

echo [Build 25%%] Baking assets...
rem The pack goes inside the .apk, where SDL_GetBasePath ("./") finds it
set "PACK=%STAGE%\assets\Assets.data"
set "ENCRYPT="
if /i "%ENABLE_ENCRYPTION%"=="TRUE" set "ENCRYPT=--encrypt"
"%TOOLS%" Bake "%OBJECTS_PATH%\BakedAssets.hpp" "!PACK!" "%ROOT%\Program\Assets" "%SHADERS_PATH%" %ENCRYPT%
if errorlevel 1 exit /b 1

rem libmain.so per CPU type: the game, which SDL's Java side loads
echo [Build 30%%] Compiling and linking libmain.so...
for %%A in (%ABIS%) do (
    call :BuildAbi %%A
    if errorlevel 1 exit /b 1
)

rem SDL's Java side (SDLActivity), compiled to classes.dex
echo [Build 60%%] Compiling SDL's Java side...
set "JAVA_DIR=%SDL_LIB_DIR%\java"
if /i not "%AUTO_ROTATE%"=="FALSE" (
    set "JAVA_DIR=%STAGE%\java"
    robocopy "%SDL_LIB_DIR%\java" "%STAGE%\java" /E /NFL /NDL /NJH /NJS /NP >nul
    powershell -NoProfile -Command "$f = $env:STAGE + '\java\org\libsdl\app\SDLActivity.java'; $t = [IO.File]::ReadAllText($f); $n = $t.Replace('SCREEN_ORIENTATION_FULL_USER', 'SCREEN_ORIENTATION_FULL_SENSOR').Replace('SCREEN_ORIENTATION_USER_LANDSCAPE', 'SCREEN_ORIENTATION_SENSOR_LANDSCAPE').Replace('SCREEN_ORIENTATION_USER_PORTRAIT', 'SCREEN_ORIENTATION_SENSOR_PORTRAIT'); if ($n -eq $t) { exit 1 }; [IO.File]::WriteAllText($f, $n)"
    if errorlevel 1 (
        echo "[Build][Error] SDL's SDLActivity.java changed shape: AUTO_ROTATE's change to it needs updating."
        exit /b 1
    )
)
type nul > "%STAGE%\Sources.txt"
for /r "%JAVA_DIR%" %%F in (*.java) do (
    set "SOURCE_FILE=%%F"
    >> "%STAGE%\Sources.txt" echo "!SOURCE_FILE:\=/!"
)
"%JDK_BIN%\javac.exe" -nowarn -encoding UTF-8 -source 8 -target 8 -Xlint:-options -bootclasspath "%ANDROID_JAR%" -d "%STAGE%\classes" @"%STAGE%\Sources.txt"
if errorlevel 1 exit /b 1
"%JDK_BIN%\jar.exe" --create --file "%STAGE%\classes.jar" -C "%STAGE%\classes" .
if errorlevel 1 exit /b 1
call "%BUILD_TOOLS%\d8.bat" --release --min-api %MIN_SDK% --lib "%ANDROID_JAR%" --output "%STAGE%\dex" "%STAGE%\classes.jar"
if errorlevel 1 exit /b 1
copy /y "%STAGE%\dex\classes.dex" "%STAGE%\classes.dex" >nul

rem Resources: launcher icons from Program\Assets\Icon.png, compiled by aapt2, and the filled-in manifest
echo [Build 70%%] Making the icons and the manifest...
mkdir "%STAGE%\res\mipmap-anydpi-v26"
> "%STAGE%\res\mipmap-anydpi-v26\ic_launcher.xml" echo ^<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android"^>^<background android:drawable="@color/ic_launcher_background" /^>^<foreground android:drawable="@mipmap/ic_launcher_foreground" /^>^</adaptive-icon^>
set "ICON_SOURCE=%ROOT%\Program\Assets\Icon.png"
rem density, icon size, and the adaptive icon's layer with its art kept to the middle two thirds
call :Icon mdpi 48 108 72 && call :Icon hdpi 72 162 108 && call :Icon xhdpi 96 216 144 && call :Icon xxhdpi 144 324 216 && call :Icon xxxhdpi 192 432 288
if errorlevel 1 exit /b 1
set "ICON_COLOR=%ICON_BACKGROUND%"
if not defined ICON_COLOR for /f %%C in ('call "%TOOLS%" Icon "%ICON_SOURCE%" --color') do set "ICON_COLOR=%%C"
mkdir "%STAGE%\res\values"
> "%STAGE%\res\values\ic_launcher_background.xml" echo ^<resources^>^<color name="ic_launcher_background"^>%ICON_COLOR%^</color^>^</resources^>
"%BUILD_TOOLS%\aapt2.exe" compile --dir "%STAGE%\res" -o "%STAGE%\res.zip"
if errorlevel 1 exit /b 1

rem The version code: 1.2.3.4 is 102003004, up to 20.99.999.999 (Google Play's limit)
for /f "tokens=1-4 delims=." %%A in ("%PROGRAM_VERSION%.0.0.0") do (
    set /a "VERSION_CODE=%%A*100000000+%%B*1000000+%%C*1000+%%D"
    if %%A GTR 20 set "VERSION_CODE="
    if %%B GTR 99 set "VERSION_CODE="
    if %%C GTR 999 set "VERSION_CODE="
    if %%D GTR 999 set "VERSION_CODE="
)
if not defined VERSION_CODE (
    echo "[Build][Error] PROGRAM_VERSION %PROGRAM_VERSION% is too big for Android's version code: up to 20.99.999.999."
    exit /b 1
)
set "SCREEN_ORIENTATION=fullSensor"
set "SDL_ORIENTATIONS="
if /i "%ORIENTATION%"=="PORTRAIT" (set "SCREEN_ORIENTATION=sensorPortrait" & set "SDL_ORIENTATIONS=Portrait PortraitUpsideDown")
if /i "%ORIENTATION%"=="LANDSCAPE" (set "SCREEN_ORIENTATION=sensorLandscape" & set "SDL_ORIENTATIONS=LandscapeLeft LandscapeRight")
if /i "%AUTO_ROTATE%"=="FALSE" set "SCREEN_ORIENTATION=fullUser"
if /i "%AUTO_ROTATE%"=="FALSE" if /i "%ORIENTATION%"=="PORTRAIT" set "SCREEN_ORIENTATION=userPortrait"
if /i "%AUTO_ROTATE%"=="FALSE" if /i "%ORIENTATION%"=="LANDSCAPE" set "SCREEN_ORIENTATION=userLandscape"
powershell -NoProfile -Command "$t = [IO.File]::ReadAllText($env:PLATFORM_DIR + '\AndroidManifest.xml'); $t = $t.Replace('{{PACKAGE}}', $env:PROGRAM_IDENTIFIER).Replace('{{LABEL}}', [Security.SecurityElement]::Escape($env:PROGRAM_NAME)).Replace('{{VERSION_CODE}}', $env:VERSION_CODE).Replace('{{VERSION_NAME}}', $env:PROGRAM_VERSION).Replace('{{MIN_SDK}}', $env:MIN_SDK).Replace('{{TARGET_SDK}}', $env:TARGET_SDK).Replace('{{SCREEN_ORIENTATION}}', $env:SCREEN_ORIENTATION).Replace('{{SDL_ORIENTATIONS}}', $env:SDL_ORIENTATIONS); $t = [regex]::Replace($t, '(?m)^([^\r\n]*?) *<\x21-- Config\.bat: ([A-Z_]+) -->(\r?\n)', { param($m) if ([Environment]::GetEnvironmentVariable($m.Groups[2].Value) -eq 'TRUE') { $m.Groups[1].Value + $m.Groups[3].Value } else { '' } }); [IO.File]::WriteAllText($env:STAGE + '\AndroidManifest.xml', $t)"
if errorlevel 1 exit /b 1

rem Packages, as EXPORT_FORMAT asks: the .apk to install, the .aab for Google Play (release key only)
set "LINK_FLAGS=-I "%ANDROID_JAR%" --manifest "%STAGE%\AndroidManifest.xml" -A "%STAGE%\assets" -0 data "%STAGE%\res.zip""
set "KEY_PASSWORD=%ANDROID_KEY_PASSWORD%"
if not defined KEY_PASSWORD set "KEY_PASSWORD=%ANDROID_KEYSTORE_PASSWORD%"
if not defined ANDROID_KEYSTORE call :DebugKey
if errorlevel 1 exit /b 1
if not defined MAKE_APK goto :ApkDone
echo [Build 80%%] Packaging the .apk...
"%BUILD_TOOLS%\aapt2.exe" link -o "%STAGE%\unaligned.apk" %LINK_FLAGS%
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%PLATFORM_DIR%\Setup.ps1" -Package "%STAGE%\unaligned.apk" -Stage "%STAGE%"
if errorlevel 1 exit /b 1
rem Libraries stored uncompressed, so Android runs them from the .apk instead of extracting a copy
"%JDK_BIN%\jar.exe" --update --no-manifest --no-compress --file "%STAGE%\unaligned.apk" -C "%STAGE%" lib
if errorlevel 1 exit /b 1
"%BUILD_TOOLS%\zipalign.exe" -f -P 16 4 "%STAGE%\unaligned.apk" "%STAGE%\aligned.apk"
if errorlevel 1 exit /b 1
call "%BUILD_TOOLS%\apksigner.bat" sign --ks "%ANDROID_KEYSTORE%" --ks-key-alias "%ANDROID_KEY_ALIAS%" --ks-pass env:ANDROID_KEYSTORE_PASSWORD --key-pass env:KEY_PASSWORD --v4-signing-enabled false --out "%APK%" "%STAGE%\aligned.apk"
if errorlevel 1 exit /b 1
:ApkDone
if defined DEBUG_KEY (
    if defined MAKE_AAB echo "[Build] No release key, so no .aab: the .apk is signed with the Android debug key, which Google Play refuses. Run "Framework\Automations\Platforms\Android\Build.bat keystore" once to make one."
    exit /b 0
)
if not defined MAKE_AAB exit /b 0

echo [Build 90%%] Packaging the .aab for Google Play...
"%BUILD_TOOLS%\aapt2.exe" link --proto-format -o "%STAGE%\module.zip" %LINK_FLAGS%
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%PLATFORM_DIR%\Setup.ps1" -Package "%STAGE%\module.zip" -Stage "%STAGE%" -Bundle
if errorlevel 1 exit /b 1
> "%STAGE%\BundleConfig.json" echo {"compression": {"uncompressedGlob": ["assets/**.data"]}}
"%JDK_BIN%\java.exe" -jar "%BUNDLETOOL%" build-bundle --modules="%STAGE%\module.zip" --config="%STAGE%\BundleConfig.json" --output="%AAB%" --overwrite
if errorlevel 1 exit /b 1
"%JDK_BIN%\jarsigner.exe" -keystore "%ANDROID_KEYSTORE%" -storepass:env ANDROID_KEYSTORE_PASSWORD -keypass:env KEY_PASSWORD -sigalg SHA256withRSA -digestalg SHA-256 "%AAB%" "%ANDROID_KEY_ALIAS%" >nul
exit /b %errorlevel%

:DebugKey
rem No release key: the debug key every Android SDK uses, which installs on your own devices only
if not defined MAKE_APK (
    echo "[Build][Error] EXPORT_FORMAT is AAB, and the .aab needs a release key. Run "Framework\Automations\Platforms\Android\Build.bat keystore" once."
    exit /b 1
)
set "ANDROID_KEYSTORE=%ANDROID_USER_HOME%\debug.keystore"
set "ANDROID_KEY_ALIAS=androiddebugkey"
set "ANDROID_KEYSTORE_PASSWORD=android"
set "KEY_PASSWORD=android"
set "DEBUG_KEY=TRUE"
if exist "%ANDROID_KEYSTORE%" exit /b 0
if not exist "%ANDROID_USER_HOME%" mkdir "%ANDROID_USER_HOME%"
"%JDK_BIN%\keytool.exe" -genkeypair -noprompt -keystore "%ANDROID_KEYSTORE%" -storepass android -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >nul
exit /b %errorlevel%

:BuildAbi
set "TRIPLE="
if "%~1"=="arm64-v8a" set "TRIPLE=aarch64-linux-android"
if "%~1"=="armeabi-v7a" set "TRIPLE=armv7a-linux-androideabi"
if "%~1"=="x86_64" set "TRIPLE=x86_64-linux-android"
if "%~1"=="x86" set "TRIPLE=i686-linux-android"
if not defined TRIPLE (
    echo "[Build][Error] Unknown Android CPU type %~1 in ABIS. Use arm64-v8a, armeabi-v7a, x86_64 or x86."
    exit /b 1
)
set "ABI_TARGET=--target=%TRIPLE%%MIN_SDK%"
echo "[Build] %~1..."
"%TOOLS%" Compile "%ROOT%" "%OBJECTS_PATH%\%~1" "%ROOT%\Framework\Main.cpp" --pch Framework/Precompiled.hpp --gnu -- "%NDK_CLANG%" %ABI_TARGET% -std=c++20 -Oz -g -DNDEBUG -fPIC -fexceptions -frtti -ffunction-sections -fdata-sections -I"%ROOT%" -I"%OBJECTS_PATH%" -isystem "%ROOT%\Libraries\SDL3\include" -isystem "%ROOT%\Libraries" -Wno-pragma-once-outside-header
if errorlevel 1 exit /b 1
mkdir "%STAGE%\lib\%~1"
rem Unstripped into Symbols\Android\<version>\<CPU> for crash reports, stripped to ship
set "SYMBOLS=%ROOT%\Symbols\Android\%PROGRAM_VERSION%\%~1"
if not exist "%SYMBOLS%" mkdir "%SYMBOLS%"
"%NDK_CLANG%" %ABI_TARGET% -shared -o "%SYMBOLS%\libmain.so" @"%OBJECTS_PATH%\%~1\Objects.rsp" -L"%SDL_LIB_DIR%\%~1" -L"%ROOT%\Libraries" -lSDL3 -landroid -llog -static-libstdc++ -Wl,--gc-sections -Wl,--no-undefined -Wl,-soname,libmain.so -Wl,-z,max-page-size=16384
if errorlevel 1 exit /b 1
for %%S in ("%NDK_CLANG%") do "%%~dpSllvm-strip.exe" --strip-all -o "%STAGE%\lib\%~1\libmain.so" "%SYMBOLS%\libmain.so"
if errorlevel 1 exit /b 1
copy /y "%SDL_LIB_DIR%\%~1\libSDL3.so" "%STAGE%\lib\%~1\" >nul
exit /b 0

:Icon
"%TOOLS%" Icon "%ICON_SOURCE%" "%STAGE%\res\mipmap-%1\ic_launcher.png" %2 && "%TOOLS%" Icon "%ICON_SOURCE%" "%STAGE%\res\mipmap-%1\ic_launcher_foreground.png" %3 %4
exit /b %errorlevel%

:FindTools
rem The newest build tools, platform and NDK in the SDK, and Java
set "SDK="
for %%S in ("%TEMPLATE_TOOLS%\Android\Sdk") do if exist "%%~S\platforms" set "SDK=%%~fS"
if not defined SDK exit /b 1
set "BUILD_TOOLS="
for /f "delims=" %%B in ('dir /b /ad /o:n "%SDK%\build-tools" 2^>nul') do if exist "%SDK%\build-tools\%%B\d8.bat" set "BUILD_TOOLS=%SDK%\build-tools\%%B"
set "ANDROID_JAR="
set "PLATFORM_API=0"
for /f "delims=" %%P in ('dir /b /ad "%SDK%\platforms" 2^>nul') do call :ConsiderPlatform %%P
set "NDK="
for /f "delims=" %%N in ('dir /b /ad /o:n "%SDK%\ndk" 2^>nul') do if exist "%SDK%\ndk\%%N\toolchains" set "NDK=%SDK%\ndk\%%N"
if defined NDK for %%N in ("%NDK%") do "%TOOLS%" Version "%PLATFORM_DIR%\Config.bat" NDK_VERSION "%%~nxN" && exit /b 1
set "NDK_CLANG=%NDK%\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe"
set "BUNDLETOOL=%SDK%\bundletool\bundletool.jar"
set "JDK_BIN="
for %%J in ("%TEMPLATE_TOOLS%\Android\jdk") do if exist "%%~J\bin\javac.exe" set "JDK_BIN=%%~fJ\bin"
if not defined BUILD_TOOLS exit /b 1
if !PLATFORM_API! LSS 35 exit /b 1
if not exist "%NDK_CLANG%" exit /b 1
if not exist "%BUNDLETOOL%" exit /b 1
if not defined JDK_BIN exit /b 1
for %%J in ("%JDK_BIN%\..") do set "JAVA_HOME=%%~fJ"
set "PATH=%JDK_BIN%;%PATH%"
exit /b 0

:ConsiderPlatform
rem %1 is a folder in platforms\, like android-36 or android-36.1: keeps the newest one that has android.jar
set "API="
for /f "tokens=2 delims=-." %%N in ("%~1") do set "API=%%N"
if not exist "%SDK%\platforms\%~1\android.jar" exit /b 0
set /a "API_NUMBER=API" 2>nul
if not "%API_NUMBER%"=="%API%" exit /b 0
if %API% GEQ %PLATFORM_API% (
    set "PLATFORM_API=%API%"
    set "ANDROID_JAR=%SDK%\platforms\%~1\android.jar"
)
exit /b 0

:OpenSetup
echo "[Build][Error] Android builds need Java, the Android SDK and NDK, SDL for Android, and DXC for the shaders. Android's setup opened in a new window: build again when it is done."
start "Android setup" cmd /c ""%~f0" setup"
exit /b 1

:setup
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup.ps1"
pause
exit /b 0

:keystore
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup.ps1" -Keystore
pause
exit /b 0

:run
for %%R in ("%~dp0..\..\..\..") do set "ROOT=%%~fR"
rem Out of the build folder, so adb and the emulator never keep it in use
cd /d "%TEMP%"
call "%ROOT%\Config.bat"
set "APK=%ROOT%\Builds\Outputs\Android\Release\%PROGRAM_NAME%.apk"
set "SDK="
for %%S in ("%TEMPLATE_TOOLS%\Android\Sdk") do if exist "%%~S\platform-tools\adb.exe" set "SDK=%%~fS"
if not exist "%APK%" (
    echo Build the Android configuration first, with EXPORT_FORMAT set to APK or BOTH: F5 installs the .apk.
    exit /b 1
)
if not defined SDK (
    echo adb was not found. Run "Framework\Automations\Platforms\Android\Build.bat setup".
    exit /b 1
)
set "ADB=%SDK%\platform-tools\adb.exe"
set "DEVICE="
call :FindDevice
if not defined DEVICE call :StartEmulator
if errorlevel 1 exit /b 1
echo Installing on %DEVICE%...
"%ADB%" -s %DEVICE% install -r "%APK%" > "%TEMP%\AdbInstall.txt" 2>&1
if not errorlevel 1 (
    del "%TEMP%\AdbInstall.txt"
    goto :Installed
)
type "%TEMP%\AdbInstall.txt"
findstr /c:"INSTALL_FAILED_UPDATE_INCOMPATIBLE" "%TEMP%\AdbInstall.txt" >nul
set "INCOMPATIBLE=!errorlevel!"
del "%TEMP%\AdbInstall.txt"
if not "!INCOMPATIBLE!"=="0" exit /b 1
echo.
echo %DEVICE% has %PROGRAM_NAME% signed with another key (the debug key, or another release key).
choice /c YN /m "Uninstall it, deleting its saved data, and install this build"
if errorlevel 2 exit /b 1
"%ADB%" -s %DEVICE% uninstall %PROGRAM_IDENTIFIER% >nul
"%ADB%" -s %DEVICE% install "%APK%" || exit /b 1
:Installed
"%ADB%" -s %DEVICE% logcat -c >nul 2>&1
"%ADB%" -s %DEVICE% shell monkey -p %PROGRAM_IDENTIFIER% -c android.intent.category.LAUNCHER 1 >nul
echo Started %PROGRAM_NAME% on %DEVICE%.
rem Still running a few seconds later, or crashed: then Android's log says why
ping -n 6 127.0.0.1 >nul
"%ADB%" -s %DEVICE% shell pidof %PROGRAM_IDENTIFIER% >nul 2>&1
if not errorlevel 1 exit /b 0
echo.
echo %PROGRAM_NAME% stopped right after starting. Android's log:
"%ADB%" -s %DEVICE% logcat -d -s SDL SDL/APP SDL/ERROR AndroidRuntime DEBUG libc
exit /b 1

:FindDevice
rem A phone shows up a few seconds after adb starts, and only once USB debugging is allowed on it
"%ADB%" start-server >nul 2>&1
for /l %%T in (1,1,5) do if not defined DEVICE (
    for /f "skip=1 tokens=1,2" %%A in ('call "%ADB%" devices') do if "%%B"=="device" if not defined DEVICE set "DEVICE=%%A"
    if not defined DEVICE ping -n 2 127.0.0.1 >nul
)
if not defined DEVICE for /f "skip=1 tokens=2" %%B in ('call "%ADB%" devices') do if "%%B"=="unauthorized" echo The phone is connected but waiting: allow USB debugging on its screen, then press F5 again.
exit /b 0

:StartEmulator
set "AVD="
if exist "%SDK%\emulator\emulator.exe" for /f "delims=" %%E in ('call "%SDK%\emulator\emulator.exe" -list-avds 2^>nul') do if not defined AVD set "AVD=%%E"
if not defined AVD (
    echo No phone is connected. Connect one with USB debugging on, or run "Framework\Automations\Platforms\Android\Build.bat setup" to install an emulator.
    exit /b 1
)
echo Starting the emulator %AVD%...
start "" "%SDK%\emulator\emulator.exe" -avd "%AVD%" -no-snapshot
"%ADB%" wait-for-device
call :FindDevice
echo Waiting for Android to start...
:Booting
set "BOOTED="
for /f "delims=" %%B in ('call "%ADB%" -s %DEVICE% shell getprop sys.boot_completed 2^>nul') do set "BOOTED=%%B"
if "%BOOTED%"=="1" for /f "delims=" %%B in ('call "%ADB%" -s %DEVICE% shell getprop init.svc.bootanim stopped 2^>nul') do set "BOOTED=%%B"
if "%BOOTED%"=="stopped" (
    ping -n 11 127.0.0.1 >nul
    exit /b 0
)
ping -n 3 127.0.0.1 >nul
goto :Booting

:sdl
rem libSDL3.so per CPU type, and SDL's matching Java side in lib\Android\java. Exit code 0 updated, 1 skipped, 2 failed
set "SDK="
for %%S in ("%TEMPLATE_TOOLS%\Android\Sdk") do if exist "%%~S\ndk" set "SDK=%%~fS"
set "NDK="
if defined SDK for /f "delims=" %%N in ('dir /b /ad /o:n "%SDK%\ndk" 2^>nul') do if exist "%SDK%\ndk\%%N\build\cmake\android.toolchain.cmake" set "NDK=%SDK%\ndk\%%N"
if not defined NDK (
    echo [Update SDL][Warning] Android skipped: the NDK is not installed. Run "Framework\Automations\Platforms\Android\Build.bat setup".
    exit /b 1
)
if not defined NINJA_TOOL (
    echo [Update SDL][Warning] Android skipped: cmake and ninja were not found. Run "Framework\Automations\Platforms\Android\Build.bat setup".
    exit /b 1
)
for %%A in (arm64-v8a armeabi-v7a x86_64 x86) do (
    echo [Update SDL] Building libSDL3.so for Android %%A...
    "%CMAKE_TOOL%" -S "%SOURCE%" -B "%WORK%\Build\Android\%%A" -G Ninja "-DCMAKE_MAKE_PROGRAM=%NINJA_TOOL%" "-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake" -DANDROID_ABI=%%A -DANDROID_PLATFORM=android-21 -DCMAKE_BUILD_TYPE=MinSizeRel "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384 -s" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_ANDROID_JAR=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF || exit /b 2
    "%CMAKE_TOOL%" --build "%WORK%\Build\Android\%%A" || exit /b 2
    if not exist "%SDL_DIR%\lib\Android\%%A" mkdir "%SDL_DIR%\lib\Android\%%A"
    copy /Y "%WORK%\Build\Android\%%A\libSDL3.so" "%SDL_DIR%\lib\Android\%%A\" >nul || exit /b 2
)
if exist "%SDL_DIR%\lib\Android\java" rmdir /s /q "%SDL_DIR%\lib\Android\java"
robocopy "%SOURCE%\android-project\app\src\main\java\org\libsdl\app" "%SDL_DIR%\lib\Android\java\org\libsdl\app" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b 2
exit /b 0
