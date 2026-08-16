# Bidhata Menu: HiBy R1 boot launcher Makefile
# Cross-compile: make CROSS=mipsel-linux-musl- STATIC=1 (musl, static)
# Native build:  make (for testing on x86)
#
# A generic, config-driven boot utility menu -- not a Game Boy emulator.
# See docs/superpowers/plans/2026-08-13-bidhata-menu-rename-and-config.md.

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
CFLAGS += -march=mips32r2 -mtune=mips32r2 -fno-pie
LDFLAGS += -static -no-pie
endif

TARGET = bidhata-menu
BOOTMENU = bootmenu
SRCS = src/main.c src/platform.c src/menu.c src/font.c src/menu_config.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(STRIP) $@

bootmenu: src/bootmenu.c
	$(CC) $(CFLAGS) -o $@ $< -lm
	$(STRIP) $@
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) $(BOOTMENU)
	$(MAKE) -C tools/compress-art clean 2>/dev/null || true

compress_art:
	$(MAKE) -C tools/compress-art $(if $(CROSS),CROSS=$(CROSS)) $(if $(STATIC),STATIC=$(STATIC))

# Push a new build to a device that already runs the launcher firmware. The
# rootfs is read-only squashfs, so this only writes to /usr/data, which the
# launcher prefers over its own /usr/bin copy.
deploy: $(TARGET)
	@echo "=== Deploying to HiBy R1 ==="
	adb root
	adb push $(TARGET) /usr/data/bidhata-menu
	adb shell chmod +x /usr/data/bidhata-menu
	@echo ""
	@echo "=== Deploy complete! ==="
	@echo ""
	@echo "To try the launcher now (stops the music player):"
	@echo "  adb shell /usr/bin/bidhata-toggle.sh launch-menu"
	@echo ""
	@echo "Boot mode:"
	@echo "  adb shell /usr/bin/bidhata-toggle.sh menu     # menu at every boot"
	@echo "  adb shell /usr/bin/bidhata-toggle.sh player  # skip the menu"
	@echo "  adb reboot"
	@echo ""
	@echo "Scripts and the fallback binary ship in the firmware image;"
	@echo "rebuild and flash it with ../repack.sh to update those."

.PHONY: all clean deploy bootmenu compress_art
