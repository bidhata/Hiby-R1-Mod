# Video, an external app for CompasOS (app protocol 1).
#
# One static MIPS executable: FFmpeg for the decoding, tinyalsa for the sound
# card, and nothing from the device but the kernel -- which is what the app
# contract asks for (https://compasplayer.org/apps.html).
#
#   make            builds build/video and the folder to copy to the card
#   make package    the same, zipped with the plugin, the licence and the README,
#                   as build/video-app.zip and as the video-app.zip kept in the repository
#
# Needs the CompasOS toolchain on PATH (mipsel-linux-musl-gcc); see the
# CompasOS README, or scripts/tools/toolchain/install_mips_toolchain.sh there.

CROSS ?= mipsel-linux-musl-
CC = $(CROSS)gcc
STRIP = $(CROSS)strip

# The CPU flags CompasOS itself is built with.
CPU_FLAGS = -march=mips32r2 -mabi=32 -mfp32 -mno-odd-spreg -ffp-contract=off

FFMPEG_VER = 9.0.2
FFMPEG_TARBALL = ffmpeg-$(FFMPEG_VER).tar.xz
FFMPEG_URL = https://ffmpeg.org/releases/$(FFMPEG_TARBALL)
FFMPEG_SRC = $(CURDIR)/ffmpeg-$(FFMPEG_VER)
FFMPEG_PREFIX = $(CURDIR)/build/ffmpeg
FFMPEG_LIB = $(FFMPEG_PREFIX)/lib/libavcodec.a

# What this device can decode in time, and nothing else: see tools/README or
# VIDEO.md for the measurements. The sound decoders are the fixed-point ones.
FFMPEG_DECODERS = mpeg4 h264 mpeg2video mpeg1video aac_fixed mp3 mp2 ac3_fixed pcm_s16le
FFMPEG_DEMUXERS = mov matroska avi
FFMPEG_PARSERS = mpeg4video h264 mpegvideo aac mpegaudio ac3

TINYALSA_DIR = $(CURDIR)/tinyalsa
# All of it, as CompasOS builds it: the plugin files are only ever asked for a
# card that does not exist, but pcm.c and mixer.c will not link without them.
TINYALSA_SRCS = $(addprefix $(TINYALSA_DIR)/src/,pcm.c pcm_hw.c pcm_plugin.c mixer.c mixer_hw.c mixer_plugin.c \
				snd_card_plugin.c limits.c)

CFLAGS = -O2 -g -Wall -Wextra -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 $(CPU_FLAGS) \
		 -ffunction-sections -fdata-sections \
		 -Isrc -I$(FFMPEG_PREFIX)/include -I$(TINYALSA_DIR)/include
LDFLAGS = -static -no-pie -Wl,--gc-sections
LIBS = $(FFMPEG_PREFIX)/lib/libavformat.a $(FFMPEG_PREFIX)/lib/libavcodec.a $(FFMPEG_PREFIX)/lib/libavutil.a \
	   -lpthread -lm

SRCS = src/main.c src/video.c src/videoresume.c src/aout.c src/screen.c src/touch.c src/draw.c
OBJS = $(SRCS:src/%.c=build/obj/%.o) $(TINYALSA_SRCS:$(TINYALSA_DIR)/src/%.c=build/obj/tinyalsa/%.o)

APP_DIR = build/video-app/video

.PHONY: all package clean distclean
all: $(APP_DIR)/video $(APP_DIR)/app.json

$(APP_DIR)/video: build/video
	@mkdir -p $(APP_DIR)
	cp build/video $@
	$(STRIP) $@
	@ls -l $@

$(APP_DIR)/app.json: app.json
	@mkdir -p $(APP_DIR)
	cp app.json $@

package: all
	cp plugin/Video.lua LICENSE README.md build/video-app/
	cd build/video-app && rm -f ../video-app.zip && python3 -m zipfile -c ../video-app.zip video Video.lua LICENSE README.md && ls -l ../video-app.zip
	cp build/video-app.zip video-app.zip

build/video: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

build/obj/%.o: src/%.c $(FFMPEG_LIB) $(TINYALSA_DIR)/include/tinyalsa/pcm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

build/obj/tinyalsa/%.o: $(TINYALSA_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 $(CPU_FLAGS) -ffunction-sections -fdata-sections -I$(TINYALSA_DIR)/include -I$(TINYALSA_DIR)/src -c $< -o $@

$(TINYALSA_DIR)/include/tinyalsa/pcm.h $(TINYALSA_SRCS):
	git clone --depth 1 https://github.com/tinyalsa/tinyalsa.git $(TINYALSA_DIR)

# --disable-everything, then the lists above. Every MIPS extension is off: the
# X1600's /proc/cpuinfo lists no ASE at all, so the decoders are plain C.
$(FFMPEG_LIB):
	@if [ ! -f $(FFMPEG_TARBALL) ]; then curl -fL -o $(FFMPEG_TARBALL) $(FFMPEG_URL); fi
	rm -rf $(FFMPEG_SRC) && tar xf $(FFMPEG_TARBALL)
	cd $(FFMPEG_SRC) && ./configure \
		--prefix=$(FFMPEG_PREFIX) \
		--enable-cross-compile --cross-prefix=$(CROSS) \
		--arch=mipsel --target-os=linux --cpu=mips32r2 \
		--extra-cflags="-mabi=32 -mfp32 -mno-odd-spreg -ffp-contract=off -ffunction-sections -fdata-sections" \
		--disable-mipsdsp --disable-mipsdspr2 --disable-msa --disable-mmi --disable-mipsfpu \
		--disable-loongson2 --disable-loongson3 \
		--enable-static --disable-shared --disable-pic \
		--disable-everything --disable-autodetect --disable-programs --disable-doc \
		--disable-network --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
		--disable-debug \
		--enable-protocol=file \
		$(addprefix --enable-decoder=,$(FFMPEG_DECODERS)) \
		$(addprefix --enable-demuxer=,$(FFMPEG_DEMUXERS)) \
		$(addprefix --enable-parser=,$(FFMPEG_PARSERS))
	$(MAKE) -C $(FFMPEG_SRC)
	$(MAKE) -C $(FFMPEG_SRC) install

clean:
	rm -rf build/obj build/video build/video-app build/video-app.zip

distclean: clean
	rm -rf build $(FFMPEG_SRC) $(TINYALSA_DIR)

-include $(OBJS:.o=.d)
