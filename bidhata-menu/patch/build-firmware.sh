#!/bin/bash
# build-firmware.sh -- stock .upt in, patched .upt out. Firmware smoothies.
#
#   ./build-firmware.sh r1_new.upt [output.upt]
#
# Unpacks, patches, repacks, leaves your tree pristine. Needs 7z, unsquashfs, mksquashfs, genisoimage.

set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SELF_DIR/../.." && pwd)"

IN_UPT=${1:-}
OUT_UPT=${2:-r1_gb.upt}

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

[ -n "$IN_UPT" ] || die "usage: $(basename "$0") <stock.upt> [output.upt]"
[ -f "$IN_UPT" ] || die "no such file: $IN_UPT"

for tool in 7z unsquashfs mksquashfs genisoimage; do
    command -v "$tool" >/dev/null || die "$tool is not installed"
done

IN_UPT=$(cd "$(dirname "$IN_UPT")" && printf '%s/%s' "$(pwd)" "$(basename "$IN_UPT")")
case "$OUT_UPT" in
    /*) ;;
    *)  OUT_UPT="$(pwd)/$OUT_UPT" ;;
esac

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "==> Unpacking $(basename "$IN_UPT")"
7z x -o"$WORK/upt" "$IN_UPT" >/dev/null
cat "$WORK/upt"/ota_v0/rootfs.squashfs.* > "$WORK/rootfs.squashfs"
cat "$WORK/upt"/ota_v0/xImage.*          > "$WORK/xImage"
unsquashfs -q -d "$WORK/squashfs-root" "$WORK/rootfs.squashfs" >/dev/null

echo "==> Patching"
"$SELF_DIR/bidhata-patch.sh" "$WORK/squashfs-root"

# Backup is for --revert on your dev tree; don't ship it -- stray files are party crashers.
find "$WORK/squashfs-root/etc/init.d" -name '*.bidhata-orig' -delete

echo "==> Repacking"
# repack.sh works on squashfs-root and xImage in the directory it runs from.
cp "$ROOT_DIR/repack.sh" "$WORK/repack.sh"
chmod +x "$WORK/repack.sh"
( cd "$WORK" && ./repack.sh >/dev/null )

mv "$WORK/r1_repacked.upt" "$OUT_UPT"
echo "==> Wrote $OUT_UPT"
ls -lh "$OUT_UPT"
