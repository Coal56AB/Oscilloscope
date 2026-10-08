#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MACHINE=$(uname -m)

case "$MACHINE" in
    armv7l|armv6l)
        TARGET=armhf
        ;;
    aarch64|arm64)
        TARGET=arm64
        ;;
    *)
        echo "Unsupported CPU architecture: $MACHINE" >&2
        echo "This package supports armv7l and aarch64 Tinker Board systems." >&2
        exit 1
        ;;
esac

export LD_LIBRARY_PATH="$SCRIPT_DIR/lib/$TARGET${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
BINARY="$SCRIPT_DIR/bin/$TARGET/scope_preview"

# FAT/exFAT can discard executable permissions. When that happens, copy the
# selected build to a Linux filesystem before starting it.
if [ ! -x "$BINARY" ]; then
    INSTALL_DIR="$HOME/.local/share/OscilGUI-TinkerBoard"
    mkdir -p "$INSTALL_DIR/bin/$TARGET" "$INSTALL_DIR/lib/$TARGET"
    cp "$BINARY" "$INSTALL_DIR/bin/$TARGET/scope_preview"
    cp "$SCRIPT_DIR/lib/$TARGET/libSDL2-2.0.so.0" "$INSTALL_DIR/lib/$TARGET/"
    chmod u+x "$INSTALL_DIR/bin/$TARGET/scope_preview"
    SCRIPT_DIR="$INSTALL_DIR"
    BINARY="$SCRIPT_DIR/bin/$TARGET/scope_preview"
    export LD_LIBRARY_PATH="$SCRIPT_DIR/lib/$TARGET${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

exec "$BINARY" --kiosk "$@"
