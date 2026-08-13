#!/bin/bash
# Installs the boot menu launcher into an unpacked HiBy R1 root filesystem.
#
#   ./bidhata-patch.sh <squashfs-root>            install
#   ./bidhata-patch.sh --revert <squashfs-root>   put the stock boot back
#   ./bidhata-patch.sh --check  <squashfs-root>   report what is installed
#
# Nothing here is specific to firmware 1.8: the boot script is located by what
# it does rather than by its name, and the patch refuses to run rather than
# guess if the layout has changed. Safe to run twice.

set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SELF_DIR/.." && pwd)"

LAUNCHER=/usr/bin/bidhata-launcher.sh
BACKUP_SUFFIX=.bidhata-orig

say()  { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- discovery --

# The init script that starts the music player. Found by content: the file name
# has a sequence number in it that a future firmware may well renumber, but
# whatever starts hiby_player.sh is the thing to redirect.
find_init_script() {
    local root=$1
    local dir="$root/etc/init.d"
    [ -d "$dir" ] || die "no $dir - is '$root' really an unpacked rootfs?"

    local matches=()
    local f
    for f in "$dir"/S*; do
        [ -f "$f" ] || continue
        # Skip our own backup, which would otherwise look like a second match.
        case "$f" in *"$BACKUP_SUFFIX") continue ;; esac
        # Either the stock script, or one this patch has already redirected.
        if grep -qE '^[A-Za-z0-9_]+=(/usr/bin/hiby_player\.sh|'"${LAUNCHER//\//\\/}"')' "$f"; then
            matches+=("$f")
        fi
    done

    [ ${#matches[@]} -gt 0 ] || die "no init script starts hiby_player.sh; firmware layout has changed, not patching"
    [ ${#matches[@]} -eq 1 ] || die "several init scripts match: ${matches[*]}"

    printf '%s\n' "${matches[0]}"
}

# Name of the variable holding the program to launch (PL01 in every firmware
# seen so far, but read it rather than assume).
init_var_name() {
    sed -nE 's/^([A-Za-z0-9_]+)=(\/usr\/bin\/hiby_player\.sh|'"${LAUNCHER//\//\\/}"')\s*$/\1/p' "$1" | head -1
}

init_var_value() {
    sed -nE 's/^[A-Za-z0-9_]+=(.*)$/\1/p' "$1" | head -1
}

# ------------------------------------------------------------------ payload --

# The MIPS binary to install: a freshly built one if present, else the prebuilt
# copy kept beside this script.
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

    if [ -f "$init$BACKUP_SUFFIX" ]; then
        say "  backup:    ${init#$root}$BACKUP_SUFFIX"
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
    # /usr/bin/bidhata-menu.conf is the bundled default -- the menu prefers
    # /usr/data/bidhata-menu.conf if a user has pushed a customized one
    # there over ADB, so overwriting this on every install is correct
    # (there's no live-device customization to clobber at this path; /usr
    # is baked into the read-only squashfs image at build time).
    [ -f "$config_src" ] && install -D -m 0644 "$config_src" "$root/usr/bin/bidhata-menu.conf"

    # Keep the stock init script so --revert works even years later. Only taken
    # the first time, so re-running cannot overwrite it with a patched copy.
    if [ ! -f "$init$BACKUP_SUFFIX" ]; then
        cp -p "$init" "$init$BACKUP_SUFFIX"
        say "  saved stock init to ${init#$root}$BACKUP_SUFFIX"
    fi

    # Point the launch variable at the launcher, leaving the rest untouched.
    sed -i -E "s|^${var}=.*|${var}=${LAUNCHER}|" "$init"

    local now; now=$(init_var_value "$init")
    [ "$now" = "$LAUNCHER" ] || die "failed to redirect $var (still '$now')"

    say "Done. The image now boots the boot menu launcher."
}

do_revert() {
    local root=$1
    local init; init=$(find_init_script "$root")

    if [ -f "$init$BACKUP_SUFFIX" ]; then
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
          "$root/usr/bin/bidhata-menu.conf"
    say "Removed the launcher files. The image boots the stock music player."
}

# -------------------------------------------------------------------- entry --

usage() {
    cat <<EOF
Install the boot menu launcher into an unpacked HiBy R1 root filesystem.

  $(basename "$0") <squashfs-root>            install
  $(basename "$0") --revert <squashfs-root>   restore the stock boot
  $(basename "$0") --check  <squashfs-root>   report what is installed

Typical use with a new firmware release:

  ./unpack.sh r1_new.upt
  ./bidhata-menu/patch/bidhata-patch.sh squashfs-root
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
