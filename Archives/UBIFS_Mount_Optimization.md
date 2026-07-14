# UBIFS Mount Optimization Tweak

## Background
The HiBy R1 uses an internal NAND flash memory chip formatted with the UBIFS (Unsorted Block Image File System). This filesystem is mounted to the user data partition where settings, libraries, and potentially local files are stored.

In the stock firmware, the mount command located in `/usr/bin/mount_ubifs.sh` (Line 181) uses the `sync` mount option:
```bash
mount -o sync -t ubifs /dev/${ubi_name}_0 $mount_path
```

### The Problem with `sync`
The `sync` flag forces the Linux kernel to commit every single file system transaction directly to the physical NAND flash immediately, bypassing the RAM page cache. While this ensures that no data is lost if the battery is physically removed without a proper shutdown, it has severe downsides for a music player:
1.  **Write Amplification:** NAND flash has a limited number of write cycles. Forcing immediate writes severely degrades the lifespan of the internal storage chip over time.
2.  **Performance Bottleneck:** The CPU must halt and wait for the flash memory to acknowledge every write operation. This makes saving settings, scanning libraries, or writing files noticeably sluggish.
3.  **Access Time Updates:** By default, Linux updates a file's "access time" (`atime`) metadata every time a file is read (e.g., when a song is played). Combined with `sync`, this means playing a song causes constant physical writes to the flash memory.

## The Tweak
We replaced the `-o sync` flag with `-o noatime`.

**Modified line in `/usr/bin/mount_ubifs.sh`:**
```bash
mount -o noatime -t ubifs /dev/${ubi_name}_0 $mount_path
```

### Benefits of `noatime`
1.  **Massive Flash Lifespan Increase:** Disabling access time updates (`noatime`) stops the OS from writing metadata every time a song is read. This eliminates tens of thousands of microscopic, unnecessary writes to the NAND flash.
2.  **System Snappiness:** By removing the `sync` flag, the OS is allowed to use RAM buffering. Writes to the filesystem are cached in RAM and flushed efficiently by the kernel in the background. The CPU no longer blocks on I/O, resulting in a much more responsive UI when saving data.
3.  **Battery Savings:** Less time spent blocking on I/O means the CPU can return to a low-power state faster. 

Because the HiBy R1 has a fixed internal battery and utilizes a controlled software shutdown procedure, removing the `sync` parameter is entirely safe and highly recommended for device longevity.
