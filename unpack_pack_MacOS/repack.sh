#!/bin/bash
set -euo pipefail

# Script to repack HiBy R1 firmware based on rockbox r1_patcher.sh logic

if [[ ! -d "squashfs-root" ]] || [[ ! -f "xImage" ]]; then
    echo "Error: Missing squashfs-root/ directory or xImage file in current directory."
    echo "Run unpack.sh first and ensure xImage remains in the path."
    exit 1
fi

OUT_PKG="r1_repacked.upt"

echo "======================================"
echo "Repacking into $OUT_PKG..."
echo "======================================"

# Determine md5 command
if command -v md5sum >/dev/null 2>&1; then
    md5_hash() { md5sum "$1" | awk '{print $1}'; }
elif command -v md5 >/dev/null 2>&1; then
    md5_hash() { md5 -q "$1"; }
else
    echo "Error: Neither md5sum nor md5 command found."
    exit 1
fi

# Determine stat command for size
if stat -c %s . >/dev/null 2>&1; then
    stat_size() { stat -c %s "$1"; }
else
    stat_size() { stat -f %z "$1"; }
fi

# Function to split and rename files sequentially
chunk_file() {
    local file="$1"
    local prefix="$2"
    
    # Run split (defaults to suffixes aa, ab, ac...)
    split -b 512k "$file" "$prefix"
    
    # Rename to .0000, .0001, etc.
    local i=0
    for f in "${prefix}"*; do
        # Skip if somehow it matches exactly the prefix
        if [ "$f" = "$prefix" ] || [ ! -e "$f" ]; then continue; fi
        
        local num=$(printf "%04d" $i)
        mv "$f" "${prefix}${num}"
        i=$((i+1))
    done
}

rm -rf __repack_tmp 2>/dev/null
mkdir -p __repack_tmp/image_contents/ota_v0

echo "1. Generating rootfs.squashfs..."
# Use -comp lzo and -all-root as expected by original format
sudo mksquashfs squashfs-root __repack_tmp/rootfs.squashfs -comp lzo -all-root >/dev/null

cd __repack_tmp/image_contents/ota_v0

# Process rootfs.squashfs
echo "2. Chunking and hashing rootfs..."
chunk_file "../../rootfs.squashfs" "rootfs.squashfs."

rootfs_md5=$(md5_hash "../../rootfs.squashfs")
rootfs_size=$(stat_size "../../rootfs.squashfs")

md5=$rootfs_md5
ota_md5_rootfs="ota_md5_rootfs.squashfs.$md5"
> "$ota_md5_rootfs"

for part in $(ls rootfs.squashfs.[0-9]* | sort); do
    md5next=$(md5_hash "$part")
    echo $md5next >> "$ota_md5_rootfs"
    mv "$part" "$part.$md5"
    md5=$md5next
done

# Process xImage
echo "3. Chunking and hashing xImage..."
cp "../../../xImage" "../../xImage"
chunk_file "../../xImage" "xImage."

ximage_md5=$(md5_hash "../../xImage")
ximage_size=$(stat_size "../../xImage")

md5=$ximage_md5
ota_md5_xImage="ota_md5_xImage.$md5"
> "$ota_md5_xImage"

for part in $(ls xImage.[0-9]* | sort); do
    md5next=$(md5_hash "$part")
    echo $md5next >> "$ota_md5_xImage"
    mv "$part" "$part.$md5"
    md5=$md5next
done

# Create meta files
echo "4. Generating OTA metadata..."
echo "ota_version=0

img_type=kernel
img_name=xImage
img_size=$ximage_size
img_md5=$ximage_md5

img_type=rootfs
img_name=rootfs.squashfs
img_size=$rootfs_size
img_md5=$rootfs_md5
" > ota_update.in

echo > ota_v0.ok

cd ../
echo "current_version=0" > ota_config.in

echo "5. Generating ISO image ($OUT_PKG) using genisoimage/mkisofs..."
# Use genisoimage or mkisofs (macOS commonly uses mkisofs via cdrtools or hdiutil)
if command -v genisoimage >/dev/null 2>&1; then
    ISO_CMD="genisoimage"
elif command -v mkisofs >/dev/null 2>&1; then
    ISO_CMD="mkisofs"
else
    echo "Error: Neither genisoimage nor mkisofs found."
    echo "On macOS, install via: brew install cdrtools"
    exit 1
fi

$ISO_CMD -f -U -J -joliet-long -r -allow-lowercase -allow-multidot -o "../../../$OUT_PKG" . >/dev/null 2>&1

cd ../../../
rm -rf __repack_tmp

echo "======================================"
echo "✅ Repack complete! Flash $OUT_PKG to the device."
