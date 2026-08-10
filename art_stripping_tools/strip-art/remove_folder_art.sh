#!/bin/sh
# Deletes standalone cover-art image files (folder.jpg, cover.png, etc.) from
# the SD card's music library. Complements strip_art/strip_art_all.sh, which
# only handles art embedded inside FLAC/MP3 files -- this catches the other
# common source: a separate image file sitting in the album folder.
#
# Matches only well-known standalone cover-art basenames (folder, cover,
# albumart, albumartsmall, front, back, artwork), not anything looser like
# the album's own name -- a looser match risks deleting an image the user
# actually wanted kept.
#
# Defaults to a dry run: lists what it would delete without touching
# anything. Pass -f / --force to actually delete.
#
# Usage: remove_folder_art.sh [-f|--force] [directory]
#   With no directory, scans every configured SD mount point.

FORCE=0
DIR=""
for arg in "$@"; do
    case "$arg" in
        -f|--force) FORCE=1 ;;
        *) DIR="$arg" ;;
    esac
done

if [ -n "$DIR" ]; then
    roots="$DIR"
else
    roots=""
    for m in /data/mnt/sd_0 /data/mnt/sd_1 /mnt/sd_0 /mnt/sd_1; do
        [ -d "$m" ] && roots="$roots $m"
    done
fi

if [ -z "$roots" ]; then
    echo "error: no SD mount found (tried /data/mnt/sd_0, sd_1, /mnt/sd_0, sd_1)" >&2
    exit 1
fi

NAMES="folder cover albumart albumartsmall front back artwork"
EXTS="jpg jpeg png bmp"

count=0
total_bytes=0

for root in $roots; do
    echo "Scanning $root ..."

    find_args=""
    first=1
    for n in $NAMES; do
        for e in $EXTS; do
            if [ $first -eq 1 ]; then
                find_args="-iname ${n}.${e}"
                first=0
            else
                find_args="$find_args -o -iname ${n}.${e}"
            fi
        done
    done

    find "$root" -type f \( $find_args \) | while IFS= read -r f; do
        echo "$f"
    done > /tmp/remove_folder_art_filelist.$$

    root_deleted=0
    while IFS= read -r f; do
        size=$(wc -c < "$f" 2>/dev/null)
        size=${size:-0}
        count=$((count + 1))
        total_bytes=$((total_bytes + size))
        if [ "$FORCE" = "1" ]; then
            rm -f "$f" && { echo "deleted: $f"; root_deleted=$((root_deleted + 1)); } || echo "failed:  $f"
        else
            echo "would delete: $f"
        fi
    done < /tmp/remove_folder_art_filelist.$$
    rm -f /tmp/remove_folder_art_filelist.$$

    # Same cache hiby_player fills in from embedded art (tf_image_cache_enable)
    # also caches whatever it resolved from a standalone cover file -- clear it
    # so deleted covers stop showing up from the stale cache.
    cache_db="$root/.temp/image_cache.db"
    if [ "$root_deleted" -gt 0 ] && [ -f "$cache_db" ]; then
        rm -f "$cache_db"
        echo "Cleared stale image cache: $cache_db"
    fi
done

kb=$((total_bytes / 1024))
echo ""
if [ "$FORCE" = "1" ]; then
    echo "Done. $count file(s) deleted, ${kb} KB freed."
else
    echo "Dry run: $count file(s) would be deleted (${kb} KB). Re-run with -f/--force to actually delete them."
fi
