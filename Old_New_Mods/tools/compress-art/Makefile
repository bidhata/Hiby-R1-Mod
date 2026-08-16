# compress_art: on-device embedded/standalone cover-art shrinker.
# Native build: make (host testing)
# Cross-compile: make CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1

CROSS ?=
CC = $(CROSS)gcc
STRIP = $(CROSS)strip

CFLAGS = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS = -lm

ifeq ($(STATIC),1)
CFLAGS += -march=mips32r2 -mtune=mips32r2 -fno-pie
LDFLAGS += -static -no-pie
endif

TARGET = compress_art

all: $(TARGET)

$(TARGET): compress_art.c stb_image.h stb_image_write.h stb_image_resize2.h
	$(CC) $(CFLAGS) -o $@ compress_art.c $(LDFLAGS)
	$(STRIP) $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
