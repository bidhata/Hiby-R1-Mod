#!/bin/bash
set -euo pipefail

# Script to unpack HiBy R1 firmware (.upt) based on rockbox r1_patcher.sh logic

if [[ $# -eq 1 ]]; then
    UPT_FILE="$1"
else
    UPT_FILE="r1.upt"
fi

if [[ ! -f "$UPT_FILE" ]]; then
    echo "Error: Cannot find $UPT_FILE in current directory."
    echo "Usage: ./unpack.sh [path_to_r1.upt]"
    exit 1
fi

echo "======================================"
echo "Unpacking $UPT_FILE..."
echo "======================================"

rm -rf __unpack_tmp 2>/dev/null
mkdir -p __unpack_tmp

if command -v 7z >/dev/null 2>&1; then
    SEVENZ_CMD="7z"
elif command -v 7zz >/dev/null 2>&1; then
    SEVENZ_CMD="7zz"
else
    echo "Error: 7z command not found. Please install p7zip (or 7zip on newer macOS: brew install 7zip)."
    exit 1
fi

if ! $SEVENZ_CMD x -o"__unpack_tmp" "$UPT_FILE"; then
    echo "Error: 7-Zip failed to extract $UPT_FILE. Please verify the file is a valid firmware package."
    exit 1
fi

echo "Reconstructing xImage..."
cat __unpack_tmp/ota_v0/xImage.* > xImage
echo "Reconstructing rootfs.squashfs..."
cat __unpack_tmp/ota_v0/rootfs.squashfs.* > rootfs.squashfs

echo "Extracting rootfs.squashfs to squashfs-root (Requires sudo)..."
rm -rf squashfs-root 2>/dev/null
sudo unsquashfs -f -d squashfs-root rootfs.squashfs

rm -rf __unpack_tmp
echo "✅ Unpack complete!"
echo "Original filesystem extracted to: squashfs-root/"
echo "Kernel saved as: xImage"
echo "You can now modify files in squashfs-root/"
