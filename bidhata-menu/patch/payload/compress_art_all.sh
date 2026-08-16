#!/bin/sh
# Runs compress_art over every .flac/.mp3 on the SD card's music library,
# shrinking oversized embedded cover art instead of deleting it (see
# strip_art_all.sh for the delete-based sibling). Safe to interrupt or
# re-run: compress_art never touches a file whose art already fits, and
# every write is atomic (temp file + rename), so a power loss mid-run
# leaves whatever it was working on untouched, not half-written.
#
# Usage: compress_art_all.sh [directory]
#   With no argument, scans every configured SD mount point.

COMPRESS=/usr/bin/compress_art
[ -x "$COMPRESS" ] || COMPRESS=/usr/data/compress_art

if [ ! -x "$COMPRESS" ]; then
    echo "error: compress_art binary not found at /usr/bin or /usr/data" >&2
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
compressed=0
skipped=0
errors=0

for root in $roots; do
    echo "Scanning $root ..."
    find "$root" -type f \( -iname '*.flac' -o -iname '*.mp3' \) | while IFS= read -r f; do
        echo "$f"
    done > /tmp/compress_art_filelist.$$

    # One progress line per file, so the caller can show movement instead of
    # a screen that sits unchanged for minutes (bidhata-menu reads these
    # through popen and repaints its status line on each one). The binary's
    # own stdout is suppressed: it is block-buffered through a pipe, so it
    # would arrive in unhelpful bursts and fight with these lines.
    root_total=$(wc -l < /tmp/compress_art_filelist.$$)
    root_total=${root_total:-0}
    n=0

    root_compressed=0
    while IFS= read -r f; do
        total=$((total + 1))
        n=$((n + 1))
        echo "$n/$root_total  ${f##*/}"
        "$COMPRESS" "$f" >/dev/null
        rc=$?
        case $rc in
            0) compressed=$((compressed + 1)); root_compressed=$((root_compressed + 1)) ;;
            1) skipped=$((skipped + 1)) ;;
            *) errors=$((errors + 1)) ;;
        esac
    done < /tmp/compress_art_filelist.$$
    rm -f /tmp/compress_art_filelist.$$

    # Same cache hiby_player fills in from embedded art (tf_image_cache_enable)
    # keeps showing the old picture until this is cleared -- the file's own
    # art changed size/bytes even though it's still nominally "there".
    cache_db="$root/.temp/image_cache.db"
    if [ "$root_compressed" -gt 0 ] && [ -f "$cache_db" ]; then
        rm -f "$cache_db"
        echo "Cleared stale image cache: $cache_db"
    fi
done

echo ""
echo "Done. $total files scanned, $compressed compressed, $skipped already small enough, $errors skipped on error."
