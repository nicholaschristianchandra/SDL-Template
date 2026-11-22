#!/usr/bin/env bash
# Run by Build.bat in WSL; works on a Linux machine too.
#   bash Linux.sh run <build folder> <program name>      copies the build into WSL's files, where it can run, and runs it
#   bash Linux.sh appimage <build folder> <program name> <icon .png> <output file>   packs the build as an AppImage
#   bash Linux.sh sdl <SDL source> [<output folder>]     builds libSDL3.a with zig against glibc 2.31, like the game
set -euo pipefail

if [ "${1:-}" = run ]; then
    # The libraries SDL opens for windows, graphics and sound: library, then its package (newer Ubuntu names first)
    missing=()
    for entry in libX11.so.6:libx11-6 libXext.so.6:libxext6 libXcursor.so.1:libxcursor1 libXrandr.so.2:libxrandr2 \
                 libXi.so.6:libxi6 libXss.so.1:libxss1 libxkbcommon.so.0:libxkbcommon0 libwayland-client.so.0:libwayland-client0 \
                 libwayland-cursor.so.0:libwayland-cursor0 libwayland-egl.so.1:libwayland-egl1 libdecor-0.so.0:libdecor-0-0 \
                 libEGL.so.1:libegl1 libGL.so.1:libgl1,libgl1-mesa-dri libGLESv2.so.2:libgles2 libasound.so.2:libasound2t64/libasound2 \
                 libpulse.so.0:libpulse0; do
        ldconfig -p | awk '{ print $1 }' | grep -Fx "${entry%%:*}" >/dev/null || missing+=("${entry#*:}")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        echo "Installing the libraries SDL opens for windows, graphics and sound (asks for your WSL password)..."
        sudo apt-get update -qq
        packages=()
        for choices in "${missing[@]}"; do
            for package in ${choices//,/ }; do
                for name in ${package//\// }; do
                    if apt-cache show "$name" >/dev/null 2>&1; then packages+=("$name"); break; fi
                done
            done
        done
        sudo apt-get install -y -qq "${packages[@]}"
    fi
    # WSL: X11 and software OpenGL, since its Wayland and GPU OpenGL can leave SDL's window black and frozen
    if grep -qi microsoft /proc/version; then export SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1; fi
    RUN="$(mktemp -d)"
    trap 'rm -rf "$RUN"' EXIT
    cp -r "$2"/. "$RUN"/ && cd "$RUN" && chmod +x "$3" && ./"$3"
    exit $?
fi
if [ "${1:-}" = appimage ]; then
    # One file that runs on its own: AppImage's runtime, then the build folder as a SquashFS image
    BUILD="$2" NAME="$3" ICON="$4" OUTPUT="$5"
    command -v mksquashfs >/dev/null || {
        echo "Installing squashfs-tools, which packs the AppImage (asks for your WSL password)..."
        sudo apt-get update -qq && sudo apt-get install -y -qq squashfs-tools >/dev/null
    }
    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    RUNTIME="$WORK/runtime-x86_64"
    curl -fsSL -o "$RUNTIME" https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64 || {
        echo "[AppImage][Error] Could not download AppImage's runtime."; exit 1; }
    APPDIR="$WORK/AppDir"
    ID="$(printf '%s' "$NAME" | tr -c 'A-Za-z0-9._-' '-')"
    mkdir -p "$APPDIR" && chmod 755 "$APPDIR"
    cp -r "$BUILD"/. "$APPDIR"/
    chmod +x "$APPDIR/$NAME"
    ln -s "$NAME" "$APPDIR/AppRun"
    printf '[Desktop Entry]\nType=Application\nName=%s\nExec=AppRun\nIcon=%s\nCategories=Game;\n' "$NAME" "$ID" > "$APPDIR/$ID.desktop"
    if [ -f "$ICON" ]; then cp "$ICON" "$APPDIR/$ID.png"; ln -s "$ID.png" "$APPDIR/.DirIcon"; fi
    mksquashfs "$APPDIR" "$WORK/Image" -root-owned -noappend -quiet >/dev/null
    cat "$RUNTIME" "$WORK/Image" > "$OUTPUT"
    chmod +x "$OUTPUT" 2>/dev/null || true
    exit 0
fi
[ "${1:-}" = sdl ] || { echo "Usage: bash Linux.sh run|appimage|sdl ..."; exit 1; }

GLIBC=2.31
SOURCE="$(cd "${2:?Usage: bash Linux.sh sdl <SDL source folder> [<output folder>]}" && pwd)"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT="${3:-$HERE/../../../../Libraries/SDL3/lib/Linux/x64}"
WORK="$(mktemp -d)"
trap '[ -n "${KEEP_WORK:-}" ] || rm -rf "$WORK"' EXIT
export ZIG_GLOBAL_CACHE_DIR="$WORK/zig-cache"

if command -v zig >/dev/null; then
    ZIG=zig
elif python3 -m ziglang version >/dev/null 2>&1; then
    ZIG="python3 -m ziglang"
else
    echo "[BuildSDL][Error] zig was not found. Install it: sudo snap install zig --classic --beta, or pip install ziglang, or from ziglang.org."
    exit 1
fi
command -v cmake >/dev/null || { echo "[BuildSDL][Error] cmake was not found."; exit 1; }

if command -v apt-get >/dev/null && [ "${SKIP_PACKAGES:-}" != "1" ]; then
    echo "[BuildSDL] Installing the headers SDL needs (sudo)..."
    sudo apt-get install -y cmake ninja-build pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
        libxss-dev libxfixes-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev \
        libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev libdrm-dev libgbm-dev >/dev/null
fi

# zig brings its own glibc 2.31 headers. Only the libraries' own headers are taken from /usr/include, through links,
# so the system's newer glibc headers never mix in.
HEADERS="$WORK/Headers"
mkdir -p "$HEADERS"
for entry in /usr/include/X11 /usr/include/alsa /usr/include/pulse /usr/include/xkbcommon /usr/include/libdrm \
             /usr/include/gbm.h /usr/include/xf86drm.h /usr/include/xf86drmMode.h /usr/include/libudev.h \
             /usr/include/wayland-*.h /usr/include/pipewire-0.3 /usr/include/spa-0.2 /usr/include/dbus-1.0 \
             /usr/include/libdecor-0 /usr/include/xcb /usr/include/GL /usr/include/EGL /usr/include/GLES2 \
             /usr/include/GLES3 /usr/include/KHR /usr/lib/x86_64-linux-gnu/dbus-1.0/include/dbus; do
    [ -e "$entry" ] && ln -sf "$entry" "$HEADERS/"
done

mkdir -p "$WORK/Bin"
# The links come after zig's own headers (-idirafter), and /usr/include itself is dropped when CMake adds it.
cat > "$WORK/Bin/cc" <<CC
#!/usr/bin/env bash
arguments=()
skip=0
for argument in "\$@"; do
    if [ \$skip = 1 ]; then skip=0; [ "\$argument" = /usr/include ] && continue; arguments+=(-isystem); fi
    case "\$argument" in
        -isystem) skip=1; continue ;;
        -I/usr/include|-isystem/usr/include) continue ;;
    esac
    arguments+=("\$argument")
done
exec $ZIG cc -target x86_64-linux-gnu.$GLIBC "\${arguments[@]}" -idirafter "$HEADERS"
CC
cat > "$WORK/Bin/ar" <<AR
#!/usr/bin/env bash
exec $ZIG ar "\$@"
AR
cat > "$WORK/Bin/ranlib" <<RANLIB
#!/usr/bin/env bash
exec $ZIG ranlib "\$@"
RANLIB
chmod +x "$WORK/Bin/"*

GENERATOR="Unix Makefiles"
command -v ninja >/dev/null && GENERATOR=Ninja

echo "[BuildSDL] Building libSDL3.a for x64 (glibc $GLIBC)..."
cmake -S "$SOURCE" -B "$WORK/Build" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_C_COMPILER="$WORK/Bin/cc" -DCMAKE_AR="$WORK/Bin/ar" -DCMAKE_RANLIB="$WORK/Bin/ranlib" \
    -DCMAKE_C_FLAGS="-ffunction-sections -fdata-sections" \
    -DCMAKE_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu;/usr/lib64;/usr/lib" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL=OFF \
    -DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_ALSA=ON -DSDL_PULSEAUDIO=ON -DSDL_PIPEWIRE=ON \
    > "$WORK/Configure.log" || { cat "$WORK/Configure.log"; exit 1; }
grep -E "SDL_(X11|WAYLAND|ALSA|PULSEAUDIO|PIPEWIRE|VULKAN|OPENGL) " "$WORK/Configure.log" || true
cmake --build "$WORK/Build" --parallel

mkdir -p "$OUTPUT"
cp "$WORK/Build/libSDL3.a" "$OUTPUT/libSDL3.a"
echo "[BuildSDL] Done: $OUTPUT/libSDL3.a"
