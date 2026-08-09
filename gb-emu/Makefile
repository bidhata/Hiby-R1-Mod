# HiBy R1 Game Boy Emulator Makefile
# Cross-compile: make CROSS=mipsel-linux-musl- STATIC=1 (musl, static, no ALSA)
# Native build:  make (for testing on x86)

CROSS ?= 
CC = $(CROSS)gcc
STRIP = $(CROSS)strip

CFLAGS = -O2 -Wall -Wextra -Iinclude -std=c11 -D_GNU_SOURCE
LDFLAGS = 
LIBS = -lm

# Static musl build: no ALSA (device lacks mipsel libasound), fully static.
# Built as a plain EXEC rather than static-PIE, and for mips32r2 hard-float, to
# match the ABI of the stock binaries (see readelf -h /usr/bin/hiby_player).
ifeq ($(STATIC),1)
CFLAGS += -UGB_USE_ALSA -march=mips32r2 -mtune=mips32r2 -fno-pie
LDFLAGS += -static -no-pie
else
LIBS += -lasound
CFLAGS += -DGB_USE_ALSA
endif

TARGET = gb-emu
BOOTMENU = bootmenu
TESTER = gb-headless
CORE_SRCS = src/gb.c src/cpu.c src/ppu.c src/mmu.c src/apu.c
SRCS = src/main.c $(CORE_SRCS) src/platform.c src/menu.c src/font.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(STRIP) $@

# Headless runner: boots a ROM and reports frame/checksum state, no framebuffer.
$(TESTER): src/headless.c $(CORE_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

bootmenu: src/bootmenu.c
	$(CC) $(CFLAGS) -o $@ $< -lm
	$(STRIP) $@
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) $(BOOTMENU) $(TESTER)

# Push a new build to a device that already runs the launcher firmware. The
# rootfs is read-only squashfs, so this only writes to /usr/data, which the
# launcher prefers over its own /usr/bin copy.
deploy: $(TARGET)
	@echo "=== Deploying to HiBy R1 ==="
	adb root
	adb push $(TARGET) /usr/data/gb-emu
	adb shell chmod +x /usr/data/gb-emu
	@echo ""
	@echo "=== Deploy complete! ==="
	@echo ""
	@echo "To try the launcher now (stops the music player):"
	@echo "  adb shell /usr/bin/gb-toggle.sh launch-emu"
	@echo ""
	@echo "Boot mode:"
	@echo "  adb shell /usr/bin/gb-toggle.sh emu     # menu at every boot"
	@echo "  adb shell /usr/bin/gb-toggle.sh player  # skip the menu"
	@echo "  adb reboot"
	@echo ""
	@echo "Scripts and the fallback binary ship in the firmware image;"
	@echo "rebuild and flash it with ../repack.sh to update those."
	@echo ""
	@echo "Put ROMs in /data/mnt/sd_0/games/ on the SD card (or run: deploy-roms)"

# Deploy with ROM files
deploy-roms: deploy
	@echo "=== Pushing ROM files ==="
	adb shell mkdir -p /data/mnt/sd_0/games
	@echo "Copy .gb/.gbc files to device:"
	@echo "  adb push game.gb /data/mnt/sd_0/games/"
	@echo "  adb push game.gbc /data/mnt/sd_0/games/"

.PHONY: all clean deploy deploy-roms bootmenu test

# Boot a ROM headlessly for the given number of frames (default 600).
test: $(TESTER)
	./$(TESTER) $(ROM) $(FRAMES)
