## What does this PR do?

Adds a video player: a **Video** tile under More lists the card's `Video` folder and plays a file over the whole panel, turned sideways when it is wider than tall, with pause, ten-second skips, a progress bar and resume where it was left.

## Why?

The R1 and R3 Pro II have a 480x800 / 480x720 colour panel and a card slot, and nothing to watch on them. This is software decoding only, so what it can play is narrow and stated up front: MPEG-4 (ASP) up to the size of the panel at 25 fps, H.264 up to about 480x270. Files are converted on a computer first with `tools/video-convert.sh`.

## How was it tested?

- [x] On a real **HiBy R1** (see below)
- [ ] On a real **HiBy R3 Pro II** -- not tested, I do not have one
- [ ] In the simulator (`make host`) -- not built; without the FFmpeg dev packages the page is compiled out, with them it is untested

On the R1, a 90-second 800x450 MPEG-4 + AAC clip at 25 fps:

- wired output: 247-251 of 250 frames shown per 10 s, 0 audio underruns
- Bluetooth headphones (AAC, 48 kHz): 241-250 of 250 frames shown per 10 s, 0 underruns
- list, play, pause, resume, seek both ways, resume after closing, end of file returning to the list: all exercised
- the hardware-key path (`videoplay_key`) was **not** exercised
- the on-device runs above were made against FFmpeg 7.1.1; the version was then moved to 9.0.2 (latest release) and the Bluetooth runs repeated on that. The wired-output run was not repeated on 9.0.2.

Per-frame decode + conversion cost on the R1, from `tools/video-probe` (30 fps clips, FFmpeg 7.1.1):

| | 320x180 | 480x270 | 640x360 | 800x450 |
|---|---|---|---|---|
| MPEG-2 | 4.6 ms | 9.0 ms | 16.1 ms | 25.2 ms |
| MPEG-4 | 5.2 ms | 10.3 ms | 18.5 ms | 29.0 ms |
| H.264 Baseline | 9.6 ms | 20.2 ms | 37.5 ms | 59.9 ms |
| H.264 High | 11.3 ms | 21.1 ms | 39.8 ms | 66.0 ms |

## Things a reviewer should know (checklist items I cannot tick cleanly)

- **A computer is needed.** Videos have to be converted before they play; there is no way to do that on the device. The page says so when the folder is empty and when a file is too large.
- **Third-party code:** FFmpeg 9.0.2, built static by the Makefile next to FreeType and the audio libraries rather than by `tools/system-libs/build.sh`, since it is linked into the player and not installed as a shared library. I have not audited it for open CVEs. It is configured with `--disable-everything` plus: decoders mpeg4, h264, mpeg2video, mpeg1video, aac_fixed, mp3, mp2, ac3_fixed, pcm_s16le; demuxers mov, matroska, avi; no network.
- **Size:** the stripped binary grows from 9.7 MB to 13.3 MB, and that code is locked into RAM. On the R1 the 1.3.0 rootfs with this binary is 46.25 MB in a 47.19 MB partition. Dropping H.264, MPEG-1/2 and AC-3 would win some of it back.
- **Translations:** the eight new strings are in all 24 language files. The eleven non-English ones are machine translations and want a native read.
- **Shared code:** two additions to the external audio API, two to the display code in `main.c`, and the refresh-period helper moved from `gearboyplay.c` into `gui.c` so both pages use one copy.
- **Card writes:** the resume position goes to `<card>/.local/video-resume.txt`, written when a film is closed. A film can only be playing from a mounted card, but I have not tested closing one while the card is being exported over USB.
- **Formatting:** new files were run through the repo's `.clang-format`; the edited existing files were not reformatted.
- `tools/video-probe` (second commit) is measurement tooling, not part of the player; it can be dropped from the PR if unwanted.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
