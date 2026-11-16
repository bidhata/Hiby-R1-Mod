# Video: list the card's videos and play them with the Video external app

Adds `plugins/Video/` (category "Experimental").

**What it does.** A row under More (Additional Tools on players without
`ui.more_menu`) lists the `mp4`, `m4v`, `mkv`, `avi` and `mov` files in
`<card>/Video`. Tapping one hands it to the external app with id `video`
through `plugin.launch_app`. The plugin plays nothing itself.

**The app is installed separately.** It is a static MIPS program for
`.apps/video`, 3.5 MB, over the store's 1 MiB limit for a file. Source,
a prebuilt zip and instructions: https://github.com/bidhata/Hiby-R1-Mod
(folder `compas-video-player`), GPL-3.0-or-later. When the app is missing,
the plugin says so and gives that address.

**Messages.** A list row explains each case: the player cannot launch apps,
the app is not installed, the folder holds no videos.

**Tested** on a HiBy R1 running CompásOS v1.5.1: the row appears under More,
the list shows the folder, a tap starts the app with the file, the
"not installed" message appears with the app folder removed.

**Not done.** `python3 tools/build_index.py --check` has not been run: it
needs Lua 5.5, which I do not have installed. Not tested on the R3 Pro II,
the R3 II 2025 or the Tempotec V1.

A longer note about the app, and four things I ran into on the CompásOS side
while building it, is in `NOTE_TO_COMPAS_DEV.md` in the repository above.
