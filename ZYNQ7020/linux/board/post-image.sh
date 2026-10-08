#!/bin/sh
set -eu

cd "$BINARIES_DIR"
for FILE in zImage rootfs.cpio.gz oscill-zynq7020.dtb; do
    [ -s "$FILE" ] || { echo "Missing FIT input: $BINARIES_DIR/$FILE" >&2; exit 1; }
done

# dtc resolves /incbin/ paths relative to the ITS file, so stage the
# descriptor beside the generated payloads instead of referencing it in-tree.
ITS_FILE="$BINARIES_DIR/.image.its"
trap 'rm -f "$ITS_FILE"' EXIT
cp "$BR2_EXTERNAL_OSCILL_PATH/board/image.its" "$ITS_FILE"
"$HOST_DIR/bin/mkimage" -f "$ITS_FILE" image.bin
