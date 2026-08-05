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
ifeq ($(STATIC),1)
LDFLAGS += -static
CFLAGS += -UGB_USE_ALSA
else
LIBS += -lasound
CFLAGS += -DGB_USE_ALSA
endif

TARGET = gb-emu
BOOTMENU = bootmenu
SRCS = src/main.c src/cpu.c src/ppu.c src/mmu.c src/apu.c src/platform.c
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

# Deploy to HiBy R1 via ADB (persistent data partition)
deploy: $(TARGET)
	@echo "=== Deploying to HiBy R1 ==="
	adb root
	adb remount
	adb push $(TARGET) /usr/data/gb-emu
	adb shell chmod +x /usr/data/gb-emu
	adb shell mkdir -p /mnt/sd_0/games
	adb push scripts/gb-toggle.sh /usr/bin/gb-toggle.sh
	adb shell chmod +x /usr/bin/gb-toggle.sh
	@echo ""
	@echo "=== Deploy complete! ==="
	@echo ""
	@echo "To launch emulator:"
	@echo "  adb shell /usr/bin/gb-toggle.sh launch-emu"
	@echo ""
	@echo "To set emulator as default boot:"
	@echo "  adb shell /usr/bin/gb-toggle.sh emu"
	@echo "  adb reboot"
	@echo ""
	@echo "To go back to music player:"
	@echo "  adb shell /usr/bin/gb-toggle.sh player"
	@echo "  adb reboot"
	@echo ""
	@echo "Put ROMs in /mnt/sd_0/games/ on the SD card (or run: deploy-roms)"

# Deploy with ROM files
deploy-roms: deploy
	@echo "=== Pushing ROM files ==="
	adb shell mkdir -p /mnt/sd_0/games
	@echo "Copy .gb/.gbc files to device:"
	@echo "  adb push game.gb /mnt/sd_0/games/"
	@echo "  adb push game.gbc /mnt/sd_0/games/"

.PHONY: all clean deploy deploy-roms bootmenu
