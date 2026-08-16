#!/bin/bash
# bidhata-patch.sh -- the firmware surgeon. Splices the boot menu into an
# unpacked HiBy R1 rootfs. Think CRISPR, but for init scripts.
#
#   ./bidhata-patch.sh <squashfs-root>            install
#   ./bidhata-patch.sh --revert <squashfs-root>   undo (panic button)
#   ./bidhata-patch.sh --check  <squashfs-root>   are we patched?
#
# Firmware-agnostic: finds the boot script by *what it does* (starts hiby_player),
# not its name. Handles renames/renumbers gracefully, or refuses loudly.
# Safe to run twice -- idempotent like a good mathemagician.

set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SELF_DIR/.." && pwd)"

LAUNCHER=/usr/bin/bidhata-launcher.sh
BACKUP_SUFFIX=.bidhata-orig
backup_path() { printf '%s/.%s%s' "$(dirname "$1")" "$(basename "$1")" "$BACKUP_SUFFIX"; }

# Legacy GB launcher (ancient history). Recognized as valid "current state"
# so we can take over from it -- like upgrading from jQuery to React without breaking IE6.
LEGACY_LAUNCHER=/usr/bin/gb-launcher.sh

say()  { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

want_compress() {
    [ "${BIDHATA_COMPRESS:-1}" != "0" ] && [ "${BIDHATA_COMPRESS:-1}" != "no" ] && [ "${BIDHATA_COMPRESS:-1}" != "false" ]
}

# ---------------------------------------------------------------- discovery --

# Find the init script by content: whatever launches hiby_player.sh is ours to hijack.
# File name number may change across firmware versions -- we follow behaviour, not labels.
find_init_script() {
    local root=$1
    local dir="$root/etc/init.d"
    [ -d "$dir" ] || die "no $dir - is '$root' really an unpacked rootfs?"

    local matches=()
    local f
    for f in "$dir"/S*; do
        [ -f "$f" ] || continue
        case "$f" in *"$BACKUP_SUFFIX") continue ;; esac
        # The stock script, one this patch has already redirected, or one a
        # different launcher (gb-emu's) has already redirected.
        if grep -qE '^[A-Za-z0-9_]+=(/usr/bin/hiby_player\.sh|'"${LAUNCHER//\//\\/}"'|'"${LEGACY_LAUNCHER//\//\\/}"')' "$f"; then
            matches+=("$f")
        fi
    done

    [ ${#matches[@]} -gt 0 ] || die "no init script starts hiby_player.sh; firmware layout has changed, not patching"
    [ ${#matches[@]} -eq 1 ] || die "several init scripts match: ${matches[*]}"

    printf '%s\n' "${matches[0]}"
}

# Variable holding the launch target (PL01 historically, but we read, not assume).
init_var_name() {
    sed -nE 's/^([A-Za-z0-9_]+)=(\/usr\/bin\/hiby_player\.sh|'"${LAUNCHER//\//\\/}"'|'"${LEGACY_LAUNCHER//\//\\/}"')\s*$/\1/p' "$1" | head -1
}

init_var_value() {
    sed -nE 's/^[A-Za-z0-9_]+=(.*)$/\1/p' "$1" | head -1
}

# ------------------------------------------------------------------ payload --

# Find the MIPS binary -- fresh build preferred, payload fallback otherwise. Skips native x86 imposters.
find_binary() {
    local candidates=("$PROJECT_DIR/bidhata-menu" "$SELF_DIR/payload/bidhata-menu")
    local c
    for c in "${candidates[@]}"; do
        [ -f "$c" ] || continue
        # A native x86 build in the project directory is not what goes on the
        # device; skip it and fall through to the prebuilt one.
        if file -b "$c" 2>/dev/null | grep -q MIPS; then
            printf '%s\n' "$c"
            return 0
        fi
    done
    die "no MIPS bidhata-menu binary found. Build one with:
    make -C '$PROJECT_DIR' clean
    make -C '$PROJECT_DIR' CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1"
}

find_script() {
    local name=$1
    local c
    for c in "$PROJECT_DIR/scripts/$name" "$SELF_DIR/payload/$name"; do
        [ -f "$c" ] && { printf '%s\n' "$c"; return 0; }
    done
    die "missing $name"
}

find_compress_binary() {
    local candidates=("$PROJECT_DIR/tools/compress-art/compress_art" "$SELF_DIR/payload/compress_art" "$PROJECT_DIR/bidhata-menu")
    local c
    for c in "${candidates[@]}"; do
        [ -f "$c" ] || continue
        if file -b "$c" 2>/dev/null | grep -q MIPS; then
            printf '%s\n' "$c"
            return 0
        fi
        # native build fallback for testing when no MIPS cross exists
        if [ -x "$c" ] && head -c 4 "$c" 2>/dev/null | od -An -tx1 | grep -q "7f 45 4c 46"; then
            printf '%s\n' "$c"
            return 0
        fi
    done
    return 1
}

find_compress_script() {
    local name=$1
    local c
    for c in "$PROJECT_DIR/tools/compress-art/$name" "$SELF_DIR/payload/$name" "$PROJECT_DIR/scripts/$name"; do
        [ -f "$c" ] && { printf '%s\n' "$c"; return 0; }
    done
    return 1
}

# ------------------------------------------------------------------ actions --

do_check() {
    local root=$1
    local init; init=$(find_init_script "$root")
    local var;  var=$(init_var_name "$init")
    local val;  val=$(init_var_value "$init")

    say "rootfs:      $root"
    say "init script: ${init#$root}"
    say "launch var:  $var=$val"

    if [ "$val" = "$LAUNCHER" ]; then
        say "status:      PATCHED (boots the launcher)"
    elif [ "$val" = "$LEGACY_LAUNCHER" ]; then
        say "status:      legacy gb-emu launcher (boots $LEGACY_LAUNCHER, not this launcher)"
    else
        say "status:      stock (boots the music player)"
    fi

    local f
    for f in /usr/bin/bidhata-menu "$LAUNCHER" /usr/bin/bidhata-toggle.sh /usr/bin/bidhata-menu.conf; do
        if [ -f "$root$f" ]; then
            say "  present:   $f ($(stat -c%s "$root$f") bytes)"
        else
            say "  missing:   $f"
        fi
    done
    for f in /usr/bin/compress_art /usr/bin/compress_art_all.sh /usr/bin/compress_folder_art.sh; do
        if [ -f "$root$f" ]; then
            say "  present:   $f ($(stat -c%s "$root$f") bytes)"
        else
            if want_compress; then say "  missing:   $f"; else say "  omitted:   $f (BIDHATA_COMPRESS=0)"; fi
        fi
    done

    local bp; bp=$(backup_path "$init")
    if [ -f "$bp" ]; then
        say "  backup:    ${bp#$root} (hidden)"
    elif [ -f "$init$BACKUP_SUFFIX" ]; then
        say "  backup:    ${init#$root}$BACKUP_SUFFIX (legacy visible -- will race; run --revert or remove)"
    fi
}

do_install() {
    local root=$1
    local init; init=$(find_init_script "$root")
    local var;  var=$(init_var_name "$init")
    [ -n "$var" ] || die "could not read the launch variable from $init"

    local binary; binary=$(find_binary)
    local launcher_src; launcher_src=$(find_script bidhata-launcher.sh)
    local toggle_src;   toggle_src=$(find_script bidhata-toggle.sh)
    local config_src="$PROJECT_DIR/config/bidhata-menu.conf.default"

    say "Installing into $root"
    say "  binary:      $binary"
    say "  init script: ${init#$root} (variable $var)"

    install -D -m 0755 "$binary"       "$root/usr/bin/bidhata-menu"
    install -D -m 0755 "$launcher_src" "$root$LAUNCHER"
    install -D -m 0755 "$toggle_src"   "$root/usr/bin/bidhata-toggle.sh"
    # Config: /usr/data wins, then /usr/bin. Reflash never clobbers live customization (squashfs vs UBIFS, baby).
    if [ -f "$config_src" ]; then
        if want_compress; then
            install -D -m 0644 "$config_src" "$root/usr/bin/bidhata-menu.conf"
        else
            grep -v "COMPRESS" "$config_src" > "$root/usr/bin/bidhata-menu.conf.tmp"
            install -D -m 0644 "$root/usr/bin/bidhata-menu.conf.tmp" "$root/usr/bin/bidhata-menu.conf"
            rm -f "$root/usr/bin/bidhata-menu.conf.tmp"
            say "  compress rows stripped from config (BIDHATA_COMPRESS=0)"
        fi
    fi

    if want_compress; then
        local compress_bin; compress_bin=$(find_compress_binary) || true
        if [ -n "${compress_bin:-}" ] && [ -f "$compress_bin" ]; then
            install -D -m 0755 "$compress_bin" "$root/usr/bin/compress_art"
            say "  installed: /usr/bin/compress_art ($(stat -c%s "$root/usr/bin/compress_art") bytes)"
        else
            warn "no MIPS compress_art found; compress menu rows will fail until one is built"
            warn "  build: make -C '$PROJECT_DIR/tools/compress-art' CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1"
        fi
        local ca_all; ca_all=$(find_compress_script compress_art_all.sh) || true
        local ca_folder; ca_folder=$(find_compress_script compress_folder_art.sh) || true
        [ -n "${ca_all:-}" ] && install -D -m 0755 "$ca_all" "$root/usr/bin/compress_art_all.sh"
        [ -n "${ca_folder:-}" ] && install -D -m 0755 "$ca_folder" "$root/usr/bin/compress_folder_art.sh"
    fi

    # Preserve stock init as hidden dotfile -- visible backup would race rcS's S* glob (classic footgun, now defused).
    local bp; bp=$(backup_path "$init")
    if [ -f "$init$BACKUP_SUFFIX" ] && [ ! -f "$bp" ]; then
        mv -f "$init$BACKUP_SUFFIX" "$bp"
        say "  migrated legacy backup to ${bp#$root} (hidden from rcS)"
    fi
    if [ ! -f "$bp" ]; then
        cp -p "$init" "$bp"
        say "  saved stock init to ${bp#$root} (hidden from rcS)"
    fi

    # Point launch var at us. One sed to rule them all.
    sed -i -E "s|^${var}=.*|${var}=${LAUNCHER}|" "$init"

    local now; now=$(init_var_value "$init")
    [ "$now" = "$LAUNCHER" ] || die "failed to redirect $var (still '$now')"

    say "Done. The image now boots the boot menu launcher."
}

do_revert() {
    local root=$1
    local init; init=$(find_init_script "$root")

    local bp; bp=$(backup_path "$init")
    if [ -f "$bp" ]; then
        cp -p "$bp" "$init"
        rm -f "$bp" "$init$BACKUP_SUFFIX"
        say "Restored ${init#$root} from backup."
    elif [ -f "$init$BACKUP_SUFFIX" ]; then
        cp -p "$init$BACKUP_SUFFIX" "$init"
        rm -f "$init$BACKUP_SUFFIX"
        say "Restored ${init#$root} from backup."
    else
        local var; var=$(init_var_name "$init")
        [ -n "$var" ] || die "no backup, and cannot read the launch variable"
        sed -i -E "s|^${var}=.*|${var}=/usr/bin/hiby_player.sh|" "$init"
        say "No backup found; pointed $var back at hiby_player.sh."
    fi

    rm -f "$root/usr/bin/bidhata-menu" "$root$LAUNCHER" "$root/usr/bin/bidhata-toggle.sh" \
           "$root/usr/bin/bidhata-menu.conf" \
           "$root/usr/bin/compress_art" "$root/usr/bin/compress_art_all.sh" "$root/usr/bin/compress_folder_art.sh"
    say "Removed the launcher files. The image boots the stock music player."
}

# -------------------------------------------------------------------- entry --

usage() {
    cat <<EOF
Install the boot menu launcher into an unpacked HiBy R1 root filesystem.

  $(basename "$0") <squashfs-root>            install
  $(basename "$0") --revert <squashfs-root>   restore the stock boot
  $(basename "$0") --check  <squashfs-root>   report what is installed

Flags for compress (optional art shrinker, on by default):
  --no-compress   Omit compress_art + menu rows  (same as BIDHATA_COMPRESS=0)

Typical use with a new firmware release:

  ./unpack.sh r1_new.upt
  ./bidhata-menu/patch/bidhata-patch.sh squashfs-root
  BIDHATA_COMPRESS=0 ./bidhata-menu/patch/bidhata-patch.sh squashfs-root  # no compress
  ./bidhata-menu/patch/bidhata-patch.sh --no-compress squashfs-root
  ./repack.sh
EOF
}

main() {
    local action=install
    local root=""

    while [ $# -gt 0 ]; do
        case "$1" in
            --revert) action=revert ;;
            --check)  action=check ;;
            --no-compress) BIDHATA_COMPRESS=0 ;;
            -h|--help) usage; exit 0 ;;
            -*) die "unknown option: $1" ;;
            *)  root=$1 ;;
        esac
        shift
    done

    [ -n "$root" ] || { usage; exit 1; }
    [ -d "$root" ] || die "no such directory: $root"

    case "$action" in
        install) do_install "$root" ;;
        revert)  do_revert  "$root" ;;
        check)   do_check   "$root" ;;
    esac
}

main "$@"
