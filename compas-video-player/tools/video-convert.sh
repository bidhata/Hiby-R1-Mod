#!/usr/bin/env bash
# Converts a video into what the player plays: MPEG-4 with AAC stereo in an
# .mp4, no larger than the panel held sideways. Runs on a computer, not on the
# device; needs ffmpeg.
#
#   tools/video-convert.sh [options] input [input ...]
#
#     -d r1|r3      the player: r1 is 800x480 (default), r3 the R3 Pro II's 720x480
#     -o dir        where the results go (default: next to each input)
#     -a n          which sound track to keep, counting from 0 (default 0)
#     -b kbit       picture bitrate (default 1800)
#     -r fps        most frames a second to keep (default 25)
#     -s hh:mm:ss   start here instead of at the beginning
#     -t seconds    stop after this long (for trying a setting on a minute of film)
#
# The result is named after the input with the player's size in it, and goes
# into the Video folder of the card.
#
# Why these settings (the measurements are in tools/video-probe):
#   * MPEG-4, not H.264: the device decodes it three to four times faster, and
#     at the full panel size H.264 does not reach real time at all.
#   * At most 25 frames a second, never more than the source has. A full-size
#     frame costs the device about 29 ms to decode and convert, before the
#     sound and the drawing: 25 a second (40 ms each) holds, 30 does not, and
#     a film is 24 or 25 anyway. For a smaller picture, -r 30 is fine.
#   * Two B-frames between reference frames: when the device falls behind it
#     drops those first, and the picture stays whole.
#   * A key frame every two seconds: seeking lands on key frames.
#   * 44.1 kHz stereo AAC, decoded on the device in fixed point.
#
# FFMPEG=/path/to/ffmpeg picks a particular ffmpeg.
set -euo pipefail

ffmpeg="${FFMPEG:-ffmpeg}"
device=r1
outdir=""
track=0
kbit=1800
fpsmax=25
start=""
length=""

usage() {
	sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
	exit 2
}

while getopts "d:o:a:b:r:s:t:h" opt; do
	case "$opt" in
	d) device="$OPTARG" ;;
	o) outdir="$OPTARG" ;;
	a) track="$OPTARG" ;;
	b) kbit="$OPTARG" ;;
	r) fpsmax="$OPTARG" ;;
	s) start="$OPTARG" ;;
	t) length="$OPTARG" ;;
	*) usage ;;
	esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || usage

case "$device" in
r1) box_w=800 box_h=480 ;;
r3) box_w=720 box_h=480 ;;
*)
	echo "error: -d takes r1 or r3" >&2
	exit 2
	;;
esac

if ! command -v "$ffmpeg" >/dev/null 2>&1; then
	echo "error: ffmpeg not found (install it, or set FFMPEG=/path/to/ffmpeg)" >&2
	exit 1
fi

# Square pixels first (a DVD's are not), then down to the largest size that
# fits the panel with both sides even. A picture already smaller is left at its
# size: the player does not scale, and neither should this.
vf="scale='trunc(iw*sar/2)*2':ih,setsar=1"
vf="$vf,scale='min($box_w,iw)':'min($box_h,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2"
vf="$vf,format=yuv420p"

for input in "$@"; do
	if [ ! -f "$input" ]; then
		echo "skipped, no such file: $input" >&2
		continue
	fi
	base="$(basename "$input")"
	base="${base%.*}"
	dir="${outdir:-$(dirname "$input")}"
	mkdir -p "$dir"
	out="$dir/$base.$device.mp4"

	echo "converting: $input"
	echo "        to: $out"

	pre=()
	[ -n "$start" ] && pre+=(-ss "$start")
	post=()
	[ -n "$length" ] && post+=(-t "$length")

	"$ffmpeg" -hide_banner -loglevel warning -stats -y \
		"${pre[@]}" -i "$input" "${post[@]}" \
		-map 0:v:0 -map "0:a:$track?" -sn -dn \
		-vf "$vf" -fpsmax "$fpsmax" \
		-c:v mpeg4 -vtag mp4v -bf 2 -g 50 \
		-b:v "${kbit}k" -maxrate "$((kbit * 2))k" -bufsize "$((kbit * 2))k" \
		-c:a aac -b:a 128k -ac 2 -ar 44100 \
		-movflags +faststart \
		"$out"
done
