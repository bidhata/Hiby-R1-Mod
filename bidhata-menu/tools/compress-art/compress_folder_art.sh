#!/bin/sh
# Shrinks oversized standalone cover-art image files (folder.jpg, cover.png,
# etc.) in the SD card's music library instead of deleting them -- see
# remove_folder_art.sh for the delete-based sibling. Complements
# compress_art_all.sh, which only handles art embedded inside FLAC/MP3
# files.
#
# Matches only well-known standalone cover-art basenames (folder, cover,
# albumart, albumartsmall, front, back, artwork), not anything looser like
# the album's own name -- same matching rule remove_folder_art.sh uses, so
# an image the user actually wanted kept at full resolution isn't touched
# by accident. BMP files are skipped (compress_art doesn't re-encode BMP).
#
# Defaults to a dry run: lists what it would compress without touching
# anything. Pass -f / --force to actually rewrite files.
#
# Usage: compress_folder_art.sh [-f|--force] [directory]
#   With no directory, scans every configured SD mount point.

COMPRESS=/usr/bin/compress_art
[ -x "$COMPRESS" ] || COMPRESS=/usr/data/compress_art

if [ ! -x "$COMPRESS" ]; then
    echo "error: compress_art binary not found at /usr/bin or /usr/data" >&2
    exit 1
fi

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

total=0
compressed=0
skipped=0
errors=0

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
    done > /tmp/compress_folder_art_filelist.$$

    # One progress line per file -- see the same note in compress_art_all.sh.
    root_total=$(wc -l < /tmp/compress_folder_art_filelist.$$)
    root_total=${root_total:-0}
    n=0

    root_compressed=0
    while IFS= read -r f; do
        total=$((total + 1))
        n=$((n + 1))
        if [ "$FORCE" = "1" ]; then
            echo "$n/$root_total  ${f##*/}"
            "$COMPRESS" "$f" >/dev/null
            rc=$?
            case $rc in
                0) compressed=$((compressed + 1)); root_compressed=$((root_compressed + 1)) ;;
                1) skipped=$((skipped + 1)) ;;
                *) errors=$((errors + 1)) ;;
            esac
        else
            echo "would check: $f"
        fi
    done < /tmp/compress_folder_art_filelist.$$
    rm -f /tmp/compress_folder_art_filelist.$$

    # Same cache hiby_player fills in from resolved cover art also caches
    # whatever it read from a standalone cover file -- clear it so a
    # compressed cover's stale cached copy stops showing up.
    cache_db="$root/.temp/image_cache.db"
    if [ "$root_compressed" -gt 0 ] && [ -f "$cache_db" ]; then
        rm -f "$cache_db"
        echo "Cleared stale image cache: $cache_db"
    fi
done

echo ""
if [ "$FORCE" = "1" ]; then
    echo "Done. $total file(s) checked, $compressed compressed, $skipped already small enough, $errors failed."
else
    echo "Dry run: $total file(s) would be checked. Re-run with -f/--force to actually compress them."
fi
