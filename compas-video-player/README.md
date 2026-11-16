# Video for CompásOS

A video player for [CompásOS](https://github.com/Starnished66/compas-player) on
the HiBy R1, built as an **external app** plus a small **plugin**. CompásOS
itself is not modified and nothing is flashed: two things are copied to the
SD card.

- [What it does](#what-it-does)
- [What it can play](#what-it-can-play)
- [Install](#install)
- [Converting a video](#converting-a-video)
- [Using the player](#using-the-player)
- [When something does not work](#when-something-does-not-work)
- [How it works](#how-it-works)
- [Build from source](#build-from-source)
- [State and limits](#state-and-limits)
- [Credits and licences](#credits-and-licences)

## What it does

- Lists the videos in the `Video` folder of the card, under **More > Video**.
- Plays a video over the whole screen, turned sideways when it is wider than
  tall, so a film fills the panel with the player held on its side.
- Sound through the headphone socket or Bluetooth headphones.
- Pause, ten seconds back and forward, a progress bar with times, and the
  volume keys.
- Remembers where each film was left and carries on from there.
- A sound-against-picture adjustment, remembered separately for Bluetooth and
  for the headphone socket.

## What it can play

The R1 has one 1 GHz processor core and no video hardware, so everything is
decoded in software and nothing is scaled. In practice a video has to be
**converted for the screen first**, on a computer. The settings below are what
the device was measured to keep up with.

| | |
|---|---|
| Picture | MPEG-4 (the DivX/Xvid kind). H.264 only up to about 480x270 |
| Size | no larger than the screen held sideways: **800x480** on the R1 |
| Frame rate | up to 25 frames a second at full size |
| Sound | AAC, MP3, MP2 or AC-3, stereo |
| Container | `.mp4`, `.m4v`, `.mkv`, `.avi`, `.mov` |

A file larger than the screen is refused with a message rather than played
badly. HEVC (x265), VP9 and AV1 are not supported at any size. A 720p or 1080p
download will not play as it is; converting it also makes it much smaller.

## Install

You need CompásOS on the player and the two pieces from `video-app.zip`,
which sits next to this README in
[the repository](https://github.com/bidhata/Hiby-R1-Mod) (or from a build of
your own, see below). Unzip it on your computer first.

1. **The app.** Copy the `video` folder into `.apps` in the root of the card
   (create `.apps` if it is not there):

   ```
   <card>/.apps/video/app.json
   <card>/.apps/video/video
   ```

2. **The plugin.** Copy `Video.lua` into `.plugins` in the root of the card:

   ```
   <card>/.plugins/Video.lua
   ```

3. On the player: **Settings > System > Plugin Manager > Refresh Plugins**.
   `Video` appears in that list with its switch on.

4. Put converted videos in a folder called `Video` in the root of the card.

5. **More > Video**, and tap a film. The first time, CompásOS asks
   "Run Video? External apps have full access to the player." Choose **Run**.
   It asks again whenever the app's program file is replaced by a new version.

Folders whose names start with a dot are hidden by default on Windows and
macOS; turn on "show hidden files" if you cannot see `.apps` after making it.

The app also runs without the plugin, from **More > External Apps > Video**,
with a plainer list of its own.

## Converting a video

### The short version

1. Install [ffmpeg](https://ffmpeg.org/download.html) on your computer.
2. Convert:

   ```bash
   tools/video-convert.sh "My Film.mkv"
   ```

   This writes `My Film.r1.mp4` next to the original.
3. Copy the `.mp4` into `Video` on the card.

### Installing ffmpeg

| System | Command |
|---|---|
| Windows | `winget install Gyan.FFmpeg`, then open a new terminal |
| Ubuntu, Debian, WSL | `sudo apt install ffmpeg` |
| macOS | `brew install ffmpeg` |

Check it with `ffmpeg -version`.

### The script

`tools/video-convert.sh` is a bash script. Run it from a terminal on Linux and
macOS; on Windows from WSL or Git Bash, or use the plain command further down,
which needs neither.

```bash
tools/video-convert.sh [options] input [input ...]
```

| Option | What it does | Default |
|---|---|---|
| `-d r1` or `-d r3` | which player: `r1` is 800x480, `r3` is the R3 Pro II's 720x480 | `r1` |
| `-o dir` | where the results go | next to each input |
| `-a n` | which sound track to keep, counting from 0 | `0` |
| `-b kbit` | picture bitrate | `1800` |
| `-r fps` | most frames a second to keep | `25` |
| `-s hh:mm:ss` | start here instead of at the beginning | |
| `-t seconds` | stop after this long | |

The result is named after the input with the player in it: `Film.r1.mp4`.

Examples:

```bash
# A whole folder, straight onto the card (mounted as E: and seen from WSL)
tools/video-convert.sh -o /mnt/e/Video /mnt/d/Movies/*.mkv

# A film with two languages, keeping the second sound track
tools/video-convert.sh -a 1 "Film.mkv"

# One minute from the middle, to try the settings before converting two hours
tools/video-convert.sh -s 00:30:00 -t 60 "Film.mkv"
```

### Without the script

The script only runs ffmpeg. This is the same conversion in one line; it works
as it is in Windows Command Prompt, PowerShell and any other shell:

```
ffmpeg -i "Film.mkv" -map 0:v:0 -map 0:a:0? -sn -dn -vf "scale='trunc(iw*sar/2)*2':ih,setsar=1,scale='min(800,iw)':'min(480,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2,format=yuv420p" -fpsmax 25 -c:v mpeg4 -vtag mp4v -bf 2 -g 50 -b:v 1800k -maxrate 3600k -bufsize 3600k -c:a aac -b:a 128k -ac 2 -ar 44100 -movflags +faststart "Film.r1.mp4"
```

### How big the files are

About **870 MB an hour** at the defaults (1800 kbit/s of picture, 128 kbit/s of
sound). A two-hour film is about 1.3 GB. `-b 1200` brings it to about 600 MB an
hour, with a softer picture in fast scenes.

### Why these settings

| Setting | Why |
|---|---|
| MPEG-4, not H.264 | The R1 decodes a full-size MPEG-4 frame in about 15 ms and an H.264 one in about 50 ms. At 25 frames a second there are 40 ms for a frame, and it still has to be converted to the screen's format and shown |
| 25 frames a second | A full-size frame takes about 29 ms to decode and convert. That fits 25 a second and does not fit 30. A film is 24 or 25 anyway |
| Two B-frames | When the player falls behind it drops those first and the picture stays whole |
| A key frame every two seconds | Skipping lands on key frames |
| Stereo AAC, 44.1 kHz | Decoded without floating point |
| No larger than the screen | The player does not scale: a picture is shown pixel for pixel |

## Using the player

Hold the player on its side for a film that is wider than tall: the picture
and the controls are turned to match.

- **Tap the picture** for the controls. They go away after three seconds while
  the film runs, and stay while it is paused.
- **Middle:** pause and play. **Either side of it:** ten seconds back, ten
  seconds forward.
- **The cross** (top left as you look at it) closes the film.
- **Along the bottom:** where the film is, how long it is, and a bar. Drag
  along the bar, or tap it, to go anywhere in the film.
- **Play/pause, next and previous keys:** pause, and ten seconds forward and
  back. A skip lands on the key frame before the place asked for, so it is
  six to eleven seconds and not exactly ten.
- **Volume keys** change the volume, with a bar across the top while they do.
  On the headphone socket the level you had in CompásOS comes back when the
  app closes; over Bluetooth it stays where you left it.
- **The power key** pauses the film and turns the screen off. Press it again
  for the screen; the film stays paused until you press play.
- **The screen stays at your brightness** while a film plays; it does not
  dim. A film left paused for two minutes has its screen switched off, to
  save the battery. A touch, or the play or skip keys, brings it back.
- A film closed part way through **carries on from there** next time, starting
  five seconds earlier. One left in its first half minute, or in its last
  twentieth, starts from the beginning.

### Sound against picture

Bluetooth headphones delay the sound by an amount the player cannot measure,
and it differs from one pair to the next. So there is a correction you set by
eye, in the **top right of the controls**: a minus, a figure in milliseconds,
and a plus.

- Voices come **after** the lips move: press **plus**.
- Voices come **before** the lips move: press **minus**.

Each press is 50 ms. The figure is saved at once and kept between films,
separately for Bluetooth and for the headphone socket. Bluetooth starts at
+150 ms, which was right for one pair of AAC headphones (HUAWEI FreeLace
Pro 2); the headphone socket starts at 0.

## When something does not work

| What you see | What to do |
|---|---|
| No **Video** row under More | The plugin is not loaded: check `.plugins/Video.lua` is on the card and use Refresh Plugins. Up to four plugin rows fit under More |
| "The Video app is not installed" | The `video` folder is not in `.apps`, or it is named something else. The folder must be called `video` |
| "No videos. Put them in the Video folder..." | The folder must be called `Video` and sit in the root of the card |
| "Run Video?" every time | Expected once after each new version of the app; not otherwise |
| "Turn off DAC mode to run apps." | CompásOS does not start apps while it is a USB DAC |
| "Audio is still active; cannot start the app." | Stop the music and try again |
| "This video is larger than the screen" | Convert it; check `-d` matches your player |
| "This kind of video is not supported" | The picture is in a codec the player does not have (HEVC, VP9, AV1). Convert it |
| "This file cannot be opened" | The copy to the card may be incomplete. Copy it again |
| The picture stutters | Convert at the defaults, in particular `-r 25`. Full size at 30 frames a second is more than the player can do |
| No sound over Bluetooth | Make sure the headphones are connected in CompásOS before starting the film. The app decides where the sound goes when a film opens |
| Sound and lips do not match | See [Sound against picture](#sound-against-picture) |
| The screen is dim during a film | Fixed in this version: the app sets the brightness CompásOS has saved. If it is still dim, raise the brightness in CompásOS |
| Names show as `?` | Only in the app's own list (External Apps), which has a Latin font. The plugin's list shows any script |

The app writes a log of its last run to `<card>/.apps/.last-session.log`. If
you report a problem, that file says what the app opened, where the sound
went, and every ten seconds how many frames were shown and how many were late.

## How it works

**Two pieces, because of what each can do.** A CompásOS plugin is a Lua script:
it can draw lists in the player's own interface, but it cannot decode a film.
An external app is a native program: CompásOS starts it, stops its own
playback, and waits while the app owns the screen, the touch panel, the
buttons and the sound. So the plugin is the list, and the app is the player;
the plugin starts the app with the file that was tapped
(`plugin.launch_app`).

**Decoding.** [FFmpeg](https://ffmpeg.org) 9.0.2, cut down to the few decoders
this device can run in time and linked into the app. A second thread reads the
file and decodes the sound as it comes; the compressed picture waits, a few
kilobytes a frame, and each frame is decoded about a quarter of a second
before it is due. Decoded frames are half a megabyte each and the device has
under ten megabytes to spare, so only a handful ever exist at once.

**The clock is the sound.** The decoder keeps a fifth of a second of sound
waiting in the output, and a frame is shown when the sample it belongs to is
the one being heard. If the processor runs short, the decoder drops B-frames,
and then everything but key frames, before it lets the sound break.

**Picture.** Each frame is converted from YUV to the panel's RGB565 straight
into the framebuffer page that is off screen, turned a quarter turn if the
film is wider than tall, and the two pages are swapped. The controls are drawn
over the frame by the app itself, with a typeface built into the program.

**Sound.** To the headphone socket through
[tinyalsa](https://github.com/tinyalsa/tinyalsa), directly on the kernel's
sound device. To Bluetooth by piping raw sound into the device's own `aplay`,
which is how CompásOS itself reaches BlueALSA; the stream is opened at the
rate the headphones already negotiated, because opening it at another makes
BlueALSA drop and rebuild the link.

**Files the app keeps**, in `<card>/.apps-data/video/`:

| File | What |
|---|---|
| `video-resume.txt` | where each film was left, up to 100 films |
| `sync.txt` | the sound-against-picture correction: headphone socket, then Bluetooth |
| `.asoundrc` | the Bluetooth stream's description for `aplay`, rewritten each time |

## Build from source

You need the CompásOS toolchain (`mipsel-linux-musl-gcc`) on `PATH`. The
CompásOS repository installs it:

```bash
toolchain=$(scripts/tools/toolchain/install_mips_toolchain.sh "$HOME/.cache/compas-toolchain")
export PATH="$toolchain/bin:$PATH"
```

Then, in this repository:

```bash
make            # build/video-app/video/  (app.json and the program)
make package    # build/video-app.zip     (the app folder and Video.lua)
```

The first build downloads FFmpeg 9.0.2 and tinyalsa and builds both static; it
takes a few minutes. Later builds take seconds. `make package` needs Python 3.

The result is one static MIPS executable of about 3.5 MB, which is what the
app contract asks for
([External apps guide](https://compasplayer.org/apps.html)).

| Path | What |
|---|---|
| `app.json` | the app's manifest |
| `plugin/Video.lua` | the plugin: the list in the player's own interface |
| `src/main.c` | the app's own list, the playback screen, the controls |
| `src/video.c` | reading, decoding, sound/picture timing, YUV to RGB565 |
| `src/videoresume.c` | where each film was left |
| `src/aout.c` | sound out and volume, for the sound card and for Bluetooth |
| `src/screen.c` | `/dev/fb0`: two pages, swapped by panning |
| `src/touch.c` | the touch panel and the buttons, from `/dev/input` |
| `src/draw.c` | rectangles, discs, triangles and text, turned with the picture |
| `src/font_data.h` | the built-in typeface, generated by `tools/genfont.py` |
| `tools/video-convert.sh` | converts a film for the player |

## State and limits

Tested on a **HiBy R1** running CompásOS v1.5.1 with Bluetooth headphones (AAC), by
a fixed sequence run on every build: play, a 40-second pause and resume, a
drag on the bar, both skips, the volume keys, close, and reopening from the
plugin. Also the play/pause, next, previous and power keys, and starting a
film from a screen CompásOS had already dimmed.

In steady play at 800x450 and 25 frames a second the log shows every frame in
most ten-second windows and three to five missing in the rest, none late and
none discarded. The ten seconds after opening, resuming or seeking are
rougher: 200 to 240 of 250.

The keys were pressed by writing to the kernel's input devices, which is the
path a real press takes, but not by a finger.

**Not tested:**

- sound through the headphone socket since the Bluetooth work, and the volume
  keys there;
- whether the panel itself comes back after the power key (the backlight and
  the framebuffer do);
- a whole film watched through under measurement;
- the R3 Pro II, the R3 II 2025 and the Tempotec V1. The app's manifest does
  not restrict boards, so it will be offered on them, but nothing about their
  screens, sound cards or buttons has been checked.

**Not there:**

- subtitles (a film whose subtitles are part of the picture shows them);
- scaling: a video must already be no larger than the screen;
- choosing between sound tracks in the player (choose when converting, `-a`);
- changing output mid-film: connect Bluetooth headphones before starting.

## Credits and licences

- [CompásOS](https://github.com/Starnished66/compas-player), for the external
  app and plugin interfaces this is built on.
- [FFmpeg](https://ffmpeg.org), LGPL 2.1 or later, linked statically.
- [tinyalsa](https://github.com/tinyalsa/tinyalsa), BSD 3-clause.
- The built-in typeface is rendered from
  [DejaVu Sans](https://dejavu-fonts.github.io), free to embed and
  redistribute.

This project is free software under the GNU General Public License, version 3
or (at your option) any later version; see [LICENSE](LICENSE). It comes with no
warranty.

The program is statically linked with FFmpeg, which is under the LGPL: the
source in this repository, with the FFmpeg and tinyalsa versions the Makefile
fetches, is everything needed to rebuild it.
