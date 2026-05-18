# HiBy R1 Firmware Unpacker/Repacker (macOS Compatible)

This directory contains shell scripts (`unpack.sh` and `repack.sh`) for modifying HiBy R1 `.upt` firmware files. These scripts have been updated to be fully compatible with both Linux and macOS.

## Prerequisites (macOS)

Before running the scripts on macOS, you must install a few standard utilities using [Homebrew](https://brew.sh/):

1. **7-Zip** (for unpacking `.upt` files)
2. **Squashfs tools** (for manipulating the firmware file system)
3. **cdrtools** (for generating the final `.upt` ISO image via `mkisofs`)

Open your Terminal and run the following command to install the dependencies:

```bash
brew install 7zip squashfs cdrtools
```

## Usage

### Unpacking Firmware

1. Place your HiBy R1 `.upt` firmware file (e.g., `r1.upt`) in this directory.
2. Run the unpack script:
   ```bash
   ./unpack.sh r1.upt
   ```
   *(If you omit the filename, it will default to looking for `r1.upt` in the current directory).*
3. The script will extract the original filesystem to a new folder named `squashfs-root/`. You can now modify the contents (e.g., themes, fonts, `radio.txt`). The kernel image will also be extracted as `xImage`.

*(Note: The unpack process requires `sudo` privileges to correctly extract files with the proper ownership properties. It will prompt for your Mac's password.)*

### Repacking Firmware

Once you are done modifying the contents of `squashfs-root/`, you can repack the firmware:

1. Ensure the `squashfs-root/` directory and `xImage` file are still in the current directory.
2. Run the repack script:
   ```bash
   ./repack.sh
   ```
3. The script will bundle your changes and output a flashable firmware file named `r1_repacked.upt`.

*(Note: Similar to unpacking, repacking requires `sudo` to compress the file system with root permissions.)*

## macOS Specific Changes Under the Hood
- The scripts dynamically switch between `md5sum` (Linux) and `md5 -q` (macOS natively).
- The scripts check for GNU `stat` (`-c%s`) and fall back to BSD `stat` (`-f%z`) for calculating correct file boundaries.
- macOS's native `split` command lacks the `--numeric-suffixes` argument used on Linux. The script handles this internally via an automated prefix renaming loop.
- ISO generation intelligently detects and leverages `mkisofs` (shipped in macOS `cdrtools`) when `genisoimage` is unavailable.
- Safely targets `7zz` (the standard 7-zip executable on newer Homebrew versions) as an alternative to `7z`.

## Notes
- **Do not** delete or rename the `xImage` file between unpacking and repacking, as the repack script requires it to build the bootable package.
