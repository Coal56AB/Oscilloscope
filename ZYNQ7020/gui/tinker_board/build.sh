#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
OUTPUT="$PROJECT_DIR/output/tinker/OscilGUI-TinkerBoard.zip"
SDL_VERSION=2.0.8
SDL_SHA256=edc77c57308661d576e843344d8638e025a7818bff73f8fbfab09c3c5fd092ec
SDL_URL="https://www.libsdl.org/release/SDL2-$SDL_VERSION.tar.gz"
WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/oscilgui-package.XXXXXX")
PACKAGE_DIR="$WORK_DIR/OscilGUI-TinkerBoard"
JOBS=1

cleanup()
{
    rm -rf -- "$WORK_DIR"
}
trap cleanup EXIT HUP INT TERM

if command -v nproc >/dev/null 2>&1; then JOBS=$(nproc); fi

for tool in arm-linux-gnueabihf-gcc arm-linux-gnueabihf-strip \
            aarch64-linux-gnu-gcc aarch64-linux-gnu-strip \
            curl make tar zip sha256sum; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing build tool: $tool" >&2
        echo "See tinker_board/BUILDING.md" >&2
        exit 1
    fi
done

if [ ! -f /usr/include/X11/Xlib.h ] || [ ! -f /usr/include/X11/extensions/Xext.h ]; then
    echo "X11 development headers are missing." >&2
    echo "Install libx11-dev and libxext-dev; see tinker_board/BUILDING.md" >&2
    exit 1
fi

mkdir -p "$PACKAGE_DIR/bin/armhf" "$PACKAGE_DIR/bin/arm64" \
         "$PACKAGE_DIR/lib/armhf" "$PACKAGE_DIR/lib/arm64" \
         "$PACKAGE_DIR/licenses" "$PROJECT_DIR/output/tinker"

SDL_ARCHIVE="$WORK_DIR/SDL2-$SDL_VERSION.tar.gz"
echo "Downloading SDL2 $SDL_VERSION..."
curl -fL "$SDL_URL" -o "$SDL_ARCHIVE"
echo "$SDL_SHA256  $SDL_ARCHIVE" | sha256sum -c -
tar -xzf "$SDL_ARCHIVE" -C "$WORK_DIR"
SDL_SOURCE="$WORK_DIR/SDL2-$SDL_VERSION"
cp "$SCRIPT_DIR/sdl_compat_math.c" "$SDL_SOURCE/src/stdlib/sdl_compat_math.c"

build_target()
{
    target=$1
    host=$2
    compiler=$3
    stripper=$4
    build_dir="$WORK_DIR/sdl-$target"
    dummy_dir="$WORK_DIR/x11-$target"
    runtime_dir="$PACKAGE_DIR/lib/$target"
    binary_dir="$PACKAGE_DIR/bin/$target"

    echo "Building SDL2 and OscilGUI for $target..."
    mkdir -p "$build_dir" "$dummy_dir"
    printf '%s\n' 'void placeholder(void) {}' | \
        "$compiler" -shared -fPIC -x c - -Wl,-soname,libX11.so.6 \
        -o "$dummy_dir/libX11.so.6"
    printf '%s\n' 'void placeholder(void) {}' | \
        "$compiler" -shared -fPIC -x c - -Wl,-soname,libXext.so.6 \
        -o "$dummy_dir/libXext.so.6"
    ln -s libX11.so.6 "$dummy_dir/libX11.so"
    ln -s libXext.so.6 "$dummy_dir/libXext.so"

    (
        cd "$build_dir"
        CC="$compiler" "$SDL_SOURCE/configure" \
            --host="$host" --prefix=/usr \
            --x-includes=/usr/include --x-libraries="$dummy_dir" \
            --disable-static --enable-shared --disable-audio \
            --disable-video-wayland --disable-video-mir \
            --enable-video-x11 --enable-x11-shared \
            --disable-video-x11-xcursor --disable-video-x11-xdbe \
            --disable-video-x11-xinerama --disable-video-x11-xinput \
            --disable-video-x11-xrandr --disable-video-x11-scrnsaver \
            --disable-video-x11-xshape --disable-video-x11-vm \
            --disable-video-opengl --disable-video-opengles \
            --disable-video-vulkan --disable-joystick --disable-haptic \
            --disable-power --disable-libudev --disable-dbus >/dev/null
        make -j"$JOBS" >/dev/null
    )

    cp "$build_dir/build/.libs/libSDL2-2.0.so.0.8.0" \
       "$runtime_dir/libSDL2-2.0.so.0"

    "$compiler" -O2 -std=gnu11 -DDEMO_ENABLE_GENERATOR=1 \
        -I"$build_dir/include" -I"$SDL_SOURCE/include" \
        -I"$PROJECT_DIR/src" -I"$PROJECT_DIR/../display" \
        "$PROJECT_DIR/src/scope_screen.c" "$PROJECT_DIR/src/rounded_box.c" \
        "$PROJECT_DIR/../display/boot_splash.c" "$PROJECT_DIR/src/demo_signal.c" \
        "$PROJECT_DIR/src/wave_file.c" "$PROJECT_DIR/src/panel_sdl.c" \
        "$PROJECT_DIR/src/preview_sdl.c" \
        "$PROJECT_DIR/src/capture_adapter.c" "$PROJECT_DIR/src/capture.c" \
        "$PROJECT_DIR/src/capture_processor.c" "$PROJECT_DIR/src/capture_file.c" \
        "$PROJECT_DIR/src/capture_pipeline.c" "$PROJECT_DIR/src/scope_thread.c" \
        "$PROJECT_DIR/src/control_protocol.c" "$PROJECT_DIR/src/control_transport.c" \
        "$PROJECT_DIR/src/control_serial.c" \
        -Wl,--allow-shlib-undefined \
        -o "$binary_dir/scope_preview" "$runtime_dir/libSDL2-2.0.so.0" \
        -lm -ldl -lpthread -lrt

    "$stripper" "$runtime_dir/libSDL2-2.0.so.0" "$binary_dir/scope_preview"
}

build_target armhf arm-linux-gnueabihf arm-linux-gnueabihf-gcc arm-linux-gnueabihf-strip
build_target arm64 aarch64-linux-gnu aarch64-linux-gnu-gcc aarch64-linux-gnu-strip

cp "$SCRIPT_DIR/README.txt" "$PACKAGE_DIR/README.txt"
cp "$SCRIPT_DIR/run.sh" "$PACKAGE_DIR/run.sh"
cp "$SCRIPT_DIR/START.desktop" "$PACKAGE_DIR/START.desktop"
cp "$PROJECT_DIR/src/fonts/LICENSE-Inter.txt" "$PACKAGE_DIR/licenses/Inter.txt"
cp "$SDL_SOURCE/COPYING.txt" "$PACKAGE_DIR/licenses/SDL2.txt"
chmod 755 "$PACKAGE_DIR/run.sh" "$PACKAGE_DIR/START.desktop" \
          "$PACKAGE_DIR/bin/armhf/scope_preview" \
          "$PACKAGE_DIR/bin/arm64/scope_preview"

(
    cd "$PACKAGE_DIR"
    sha256sum bin/armhf/scope_preview bin/arm64/scope_preview \
              lib/armhf/libSDL2-2.0.so.0 lib/arm64/libSDL2-2.0.so.0 \
              > SHA256SUMS.txt
)

if command -v qemu-arm >/dev/null 2>&1 && command -v qemu-aarch64 >/dev/null 2>&1; then
    echo "Checking both ARM builds with QEMU..."
    LD_LIBRARY_PATH="$PACKAGE_DIR/lib/armhf" \
        qemu-arm -L /usr/arm-linux-gnueabihf \
        "$PACKAGE_DIR/bin/armhf/scope_preview" --snapshot "$WORK_DIR/armhf.bmp"
    LD_LIBRARY_PATH="$PACKAGE_DIR/lib/arm64" \
        qemu-aarch64 -L /usr/aarch64-linux-gnu \
        "$PACKAGE_DIR/bin/arm64/scope_preview" --snapshot "$WORK_DIR/arm64.bmp"
    if [ "$(sha256sum "$WORK_DIR/armhf.bmp" | cut -d' ' -f1)" != \
         "$(sha256sum "$WORK_DIR/arm64.bmp" | cut -d' ' -f1)" ]; then
        echo "ARM32 and ARM64 snapshots differ." >&2
        exit 1
    fi
fi

rm -f -- "$OUTPUT.tmp"
(
    cd "$WORK_DIR"
    zip -q -r "$OUTPUT.tmp" OscilGUI-TinkerBoard
)
mv -f -- "$OUTPUT.tmp" "$OUTPUT"

echo "Ready: $OUTPUT"
sha256sum "$OUTPUT"
