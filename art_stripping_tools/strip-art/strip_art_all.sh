#!/bin/sh
# Runs strip_art over every .flac/.mp3 on the SD card's games/, err, music
# library. Safe to interrupt or re-run: strip_art never touches a file that
# has nothing to strip, and every write is atomic (temp file + rename), so a
# power loss mid-run leaves whatever it was working on untouched, not
# half-written.
#
# Usage: strip_art_all.sh [directory]
#   With no argument, scans every configured SD mount point.

STRIP=/usr/bin/strip_art
[ -x "$STRIP" ] || STRIP=/usr/data/strip_art

if [ ! -x "$STRIP" ]; then
    echo "error: strip_art binary not found at /usr/bin or /usr/data" >&2
    exit 1
fi

if [ -n "$1" ]; then
    roots="$1"
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

total=0
stripped=0
skipped=0
errors=0

for root in $roots; do
    echo "Scanning $root ..."
    find "$root" -type f \( -iname '*.flac' -o -iname '*.mp3' \) | while IFS= read -r f; do
        echo "$f"
    done > /tmp/strip_art_filelist.$$

    root_stripped=0
    while IFS= read -r f; do
        total=$((total + 1))
        "$STRIP" "$f"
        rc=$?
        case $rc in
            0) stripped=$((stripped + 1)); root_stripped=$((root_stripped + 1)) ;;
            1) skipped=$((skipped + 1)) ;;
            *) errors=$((errors + 1)) ;;
        esac
    done < /tmp/strip_art_filelist.$$
    rm -f /tmp/strip_art_filelist.$$

    # hiby_player caches extracted cover art in its own SQLite database
    # (tf_image_cache_enable) separately from the file itself -- stripping
    # the embedded picture doesn't invalidate an entry already cached there,
    # so the player keeps showing the old art until this is cleared. Only
    # touch it if this root actually had something stripped.
    cache_db="$root/.temp/image_cache.db"
    if [ "$root_stripped" -gt 0 ] && [ -f "$cache_db" ]; then
        rm -f "$cache_db"
        echo "Cleared stale image cache: $cache_db"
    fi
done

echo ""
echo "Done. $total files scanned, $stripped stripped, $skipped already clean, $errors skipped on error."
