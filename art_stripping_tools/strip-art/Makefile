# strip_art: on-device embedded-cover-art remover.
# Native build: make (host testing)
# Cross-compile: make CROSS=/opt/mipsel-linux-musl-cross/bin/mipsel-linux-musl- STATIC=1

CROSS ?=
CC = $(CROSS)gcc
STRIP = $(CROSS)strip

CFLAGS = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS =

ifeq ($(STATIC),1)
CFLAGS += -march=mips32r2 -mtune=mips32r2 -fno-pie
LDFLAGS += -static -no-pie
endif

TARGET = strip_art

all: $(TARGET)

$(TARGET): strip_art.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	$(STRIP) $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
