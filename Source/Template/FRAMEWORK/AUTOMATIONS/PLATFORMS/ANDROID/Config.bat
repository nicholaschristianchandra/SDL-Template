rem | APK, AAB or BOTH (the .aab, for Google Play, needs the release key from "Build.bat keystore")
set "EXPORT_FORMAT=BOTH"

rem | CPU types, from arm64-v8a armeabi-v7a x86_64 x86 (arm64-v8a: phones, x86_64: the emulator)
set "ABIS=arm64-v8a x86_64"
set "MIN_SDK=24"
set "TARGET_SDK=36"

rem | How the screen turns: BOTH, PORTRAIT or LANDSCAPE
set "ORIENTATION=BOTH"
rem | TRUE turns with the phone even when auto-rotate is off, FALSE follows the phone's auto-rotate setting
set "AUTO_ROTATE=TRUE"

rem | The launcher icon's background as #RRGGBB (empty: the top of Program\Assets\Icon.png)
set "ICON_BACKGROUND="

rem | Permissions, TRUE or FALSE: VIBRATE for SDL's haptics and gamepad rumble
set "VIBRATE=TRUE"
rem | MICROPHONE for SDL's audio recording, CAMERA for SDL's camera: Android asks the player when SDL opens them
set "MICROPHONE=FALSE"
set "CAMERA=FALSE"

rem | Tool versions: the minimum version of a tool
set "NDK_VERSION="
