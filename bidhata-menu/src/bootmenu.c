#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <signal.h>
#include <time.h>

#define SCREEN_W 480
#define SCREEN_H 800
#define TIMEOUT_SEC 5

static int fb_fd = -1;
static void *fb_mem = NULL;
static struct fb_var_screeninfo vinfo;
static int fb_bpp = 32;

static inline void put_pixel(int x, int y, unsigned int color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    unsigned int *pixels = (unsigned int *)fb_mem;
    pixels[y * (vinfo.xres_virtual / vinfo.xres) + x] = color;
}

static void fill_rect(int x, int y, int w, int h, unsigned int color) {
    for (int j = y; j < y + h && j < SCREEN_H; j++)
        for (int i = x; i < x + w && i < SCREEN_W; i++)
            put_pixel(i, j, color);
}

/* Compact 5x7 bitmap font for ASCII 0x20..0x7E.
 * Each glyph is 5 bytes, MSB = leftmost pixel. */
static const unsigned char font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x00,0x00,0xFA,0x00,0x00}, /* ! */
    {0xE0,0x00,0xE0,0x00,0x00}, /* " */
    {0x28,0xFE,0x28,0xFE,0x28}, /* # */
    {0x24,0x54,0xFE,0x54,0x48}, /* $ */
    {0xC4,0xC8,0x10,0x26,0x46}, /* % */
    {0x6C,0x92,0xAA,0x44,0x0A}, /* & */
    {0x00,0x00,0xE0,0x00,0x00}, /* ' */
    {0x00,0x38,0x44,0x82,0x00}, /* ( */
    {0x00,0x82,0x44,0x38,0x00}, /* ) */
    {0x54,0x38,0xFE,0x38,0x54}, /* * */
    {0x10,0x10,0x7C,0x10,0x10}, /* + */
    {0x00,0x04,0x0A,0x00,0x00}, /* , */
    {0x10,0x10,0x10,0x10,0x10}, /* - */
    {0x00,0x00,0x04,0x00,0x00}, /* . */
    {0x04,0x08,0x10,0x20,0x40}, /* / */
    {0x7C,0x8A,0x92,0xA2,0x7C}, /* 0 */
    {0x00,0x42,0xFE,0x02,0x00}, /* 1 */
    {0x42,0x86,0x8A,0x92,0x62}, /* 2 */
    {0x84,0x82,0x92,0x92,0x6C}, /* 3 */
    {0x18,0x28,0x48,0xFE,0x08}, /* 4 */
    {0xF4,0x92,0x92,0x92,0x8C}, /* 5 */
    {0x3C,0x52,0x92,0x92,0x0C}, /* 6 */
    {0x80,0x8E,0x90,0xA0,0xC0}, /* 7 */
    {0x6C,0x92,0x92,0x92,0x6C}, /* 8 */
    {0x60,0x92,0x92,0x94,0x78}, /* 9 */
    {0x00,0x00,0x28,0x00,0x00}, /* : */
    {0x00,0x04,0x2A,0x00,0x00}, /* ; */
    {0x00,0x10,0x28,0x44,0x82}, /* < */
    {0x28,0x28,0x28,0x28,0x28}, /* = */
    {0x82,0x44,0x28,0x10,0x00}, /* > */
    {0x40,0x80,0x8A,0x90,0x60}, /* ? */
    {0x4C,0x92,0x9E,0x82,0x7C}, /* @ */
    {0x7E,0x90,0x90,0x90,0x7E}, /* A */
    {0xFE,0x92,0x92,0x92,0x6C}, /* B */
    {0x7C,0x82,0x82,0x82,0x44}, /* C */
    {0xFE,0x82,0x82,0x44,0x38}, /* D */
    {0xFE,0x92,0x92,0x92,0x82}, /* E */
    {0xFE,0x90,0x90,0x90,0x80}, /* F */
    {0x7C,0x82,0x82,0x8A,0x4E}, /* G */
    {0xFE,0x10,0x10,0x10,0xFE}, /* H */
    {0x82,0x82,0xFE,0x82,0x82}, /* I */
    {0x04,0x02,0x82,0xFC,0x80}, /* J */
    {0xFE,0x10,0x28,0x44,0x82}, /* K */
    {0xFE,0x02,0x02,0x02,0x02}, /* L */
    {0xFE,0x40,0x20,0x40,0xFE}, /* M */
    {0xFE,0x40,0x20,0x10,0xFE}, /* N */
    {0x7C,0x82,0x82,0x82,0x7C}, /* O */
    {0xFE,0x90,0x90,0x90,0x60}, /* P */
    {0x7C,0x82,0x8A,0x84,0x7A}, /* Q */
    {0xFE,0x90,0x98,0x94,0x62}, /* R */
    {0x64,0x92,0x92,0x92,0x4C}, /* S */
    {0x80,0x80,0xFE,0x80,0x80}, /* T */
    {0xFC,0x02,0x02,0x02,0xFC}, /* U */
    {0xF8,0x04,0x02,0x04,0xF8}, /* V */
    {0xFE,0x04,0x18,0x04,0xFE}, /* W */
    {0xC6,0x28,0x10,0x28,0xC6}, /* X */
    {0xC0,0x20,0x1E,0x20,0xC0}, /* Y */
    {0x86,0x8A,0x92,0xA2,0xC2}, /* Z */
    {0x00,0xFE,0x82,0x82,0x00}, /* [ */
    {0x40,0x20,0x10,0x08,0x04}, /* backslash */
    {0x00,0x82,0x82,0xFE,0x00}, /* ] */
    {0x20,0x40,0x80,0x40,0x20}, /* ^ */
    {0x02,0x02,0x02,0x02,0x02}, /* _ */
    {0x00,0x80,0x40,0x00,0x00}, /* ` */
    {0x0C,0x12,0x12,0x0E,0x02}, /* a */
    {0xFE,0x12,0x12,0x12,0x0C}, /* b */
    {0x1C,0x22,0x22,0x22,0x22}, /* c */
    {0x0C,0x12,0x12,0x12,0xFE}, /* d */
    {0x1C,0x2A,0x2A,0x2A,0x18}, /* e */
    {0x10,0x7E,0x90,0x80,0x40}, /* f */
    {0x30,0x4A,0x4A,0x4A,0x3C}, /* g */
    {0xFE,0x10,0x10,0x10,0x0E}, /* h */
    {0x00,0x12,0x5E,0x02,0x00}, /* i */
    {0x04,0x02,0x02,0x5E,0x00}, /* j */
    {0xFE,0x08,0x14,0x22,0x00}, /* k */
    {0x82,0x82,0xFE,0x02,0x02}, /* l */
    {0x3E,0x20,0x18,0x20,0x1E}, /* m */
    {0x3E,0x20,0x20,0x20,0x1E}, /* n */
    {0x1C,0x22,0x22,0x22,0x1C}, /* o */
    {0x3E,0x28,0x28,0x28,0x10}, /* p */
    {0x10,0x28,0x28,0x28,0x3E}, /* q */
    {0x3E,0x20,0x20,0x20,0x10}, /* r */
    {0x12,0x2A,0x2A,0x2A,0x24}, /* s */
    {0x20,0xFE,0x22,0x22,0x04}, /* t */
    {0x3C,0x02,0x02,0x04,0x3E}, /* u */
    {0x38,0x04,0x02,0x04,0x38}, /* v */
    {0x3C,0x02,0x0C,0x02,0x3C}, /* w */
    {0x22,0x14,0x08,0x14,0x22}, /* x */
    {0x30,0x08,0x0A,0x0A,0x3C}, /* y */
    {0x22,0x26,0x2A,0x32,0x22}, /* z */
    {0x00,0x10,0x6C,0x82,0x00}, /* { */
    {0x00,0x00,0xFE,0x00,0x00}, /* | */
    {0x00,0x82,0x6C,0x10,0x00}, /* } */
    {0x40,0x80,0x40,0x20,0x40}, /* ~ */
};

static void draw_text(int x, int y, const char *text, unsigned int color, int scale) {
    if (scale < 1) scale = 1;
    int cx = x;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        unsigned char c = *p;
        if (c < 0x20 || c > 0x7E) c = ' ';
        const unsigned char *glyph = font5x7[c - 0x20];
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph[row] & (0x80 >> col)) {
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            put_pixel(cx + col * scale + sx, y + row * scale + sy, color);
                }
            }
        }
        cx += 6 * scale;
    }
}

static void draw_button(int bx, int by, int bw, int bh,
                        unsigned int bg, unsigned int border,
                        const char *label, unsigned int text_color) {
    // Border
    fill_rect(bx, by, bw, bh, border);
    // Background
    fill_rect(bx + 3, by + 3, bw - 6, bh - 6, bg);
    // Label centered
    int tw = strlen(label) * 6 * 2;
    int tx = bx + (bw - tw) / 2;
    int ty = by + (bh - 7 * 2) / 2;
    draw_text(tx, ty, label, text_color, 2);
}

static void draw_boot_menu(int selected, int countdown) {
    // Dark background
    fill_rect(0, 0, SCREEN_W, SCREEN_H, 0xFF1A1A2E);

    // Title
    fill_rect(60, 120, 360, 3, 0xFFE94560);
    fill_rect(100, 200, 280, 3, 0xFFE94560);

    // Title text
    draw_text(140, 162, "SELECT MODE", 0xFFE94560, 2);

    // Music Player button
    unsigned int mp_bg = (selected == 0) ? 0xFF0F3460 : 0xFF16213E;
    unsigned int mp_border = (selected == 0) ? 0xFFE94560 : 0xFF533483;
    draw_button(60, 320, 360, 100, mp_bg, mp_border, "MUSIC PLAYER", 0xFFEAEAEA);

    // Music player icon placeholder (headphone shape)
    fill_rect(100, 345, 20, 40, 0xFFE94560);
    fill_rect(360, 345, 20, 40, 0xFFE94560);
    fill_rect(100, 345, 280, 15, 0xFFE94560);

    // Game Boy button
    unsigned int gb_bg = (selected == 1) ? 0xFF0F3460 : 0xFF16213E;
    unsigned int gb_border = (selected == 1) ? 0xFFE94560 : 0xFF533483;
    draw_button(60, 470, 360, 100, gb_bg, gb_border, "GAME BOY", 0xFFEAEAEA);

    // Game Boy icon placeholder (screen + buttons)
    fill_rect(180, 495, 60, 40, 0xFF9BBC0F);
    fill_rect(250, 500, 15, 10, 0xFFE94560);
    fill_rect(270, 510, 15, 10, 0xFFE94560);

    // Countdown bar
    int bar_width = (countdown * 360) / TIMEOUT_SEC;
    fill_rect(60, 650, 360, 8, 0xFF16213E);
    fill_rect(60, 650, bar_width, 8, 0xFFE94560);

    // Countdown text
    char buf[32];
    snprintf(buf, sizeof(buf), "AUTO: %ds", countdown);
    draw_text(60, 660, buf, 0xFF888888, 2);

    // Hint text
    draw_text(120, 720, "TOUCH TO SELECT", 0xFF888888, 2);
}

static int read_touch(int *tx, int *ty) {
    const char *devices[] = {"/dev/input/event0", "/dev/input/event1", "/dev/input/event2"};
    int found = 0;

    for (int d = 0; d < 3; d++) {
        int fd = open(devices[d], O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        struct input_event ev;
        int x = -1, y = -1, btn = 0;
        int got_event = 0;

        while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
            got_event = 1;
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X) x = ev.value;
                else if (ev.code == ABS_Y) y = ev.value;
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                btn = ev.value;
            }
        }

        close(fd);

        if (got_event && btn && x >= 0 && y >= 0) {
            *tx = x;
            *ty = y;
            found = 1;
            break;
        }
    }
    return found;
}

int main(int argc, char *argv[]) {
    const char *player_cmd = "/usr/bin/hiby_player";
    const char *emu_cmd = "/usr/bin/bidhata-menu";
    const char *flag_file = "/usr/data/emulator_mode";

    /* The "default" (timeout / no interaction) selection follows the
     * persistent mode flag written by bidhata-toggle.sh. */
    int default_selected = 0; /* music player */
    if (argc > 1 && strcmp(argv[1], "--default-emu") == 0) {
        default_selected = 1;
    }
    int selected = default_selected;

    /* Check if flag file exists - skip menu if "player" mode */
    if (argc > 1 && strcmp(argv[1], "--player") == 0) {
        execl(player_cmd, player_cmd, NULL);
        perror("execl player");
        return 1;
    }
    if (argc > 1 && strcmp(argv[1], "--emu") == 0) {
        unlink(flag_file);
        execl(emu_cmd, emu_cmd, NULL);
        perror("execl emu");
        return 1;
    }

    // Open framebuffer
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        perror("open fb0");
        execl(player_cmd, player_cmd, NULL);
        return 1;
    }

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("ioctl fb");
        close(fb_fd);
        execl(player_cmd, player_cmd, NULL);
        return 1;
    }

    fb_bpp = vinfo.bits_per_pixel;
    int fb_size = vinfo.xres_virtual * vinfo.yres_virtual * (fb_bpp / 8);
    fb_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        perror("mmap fb");
        close(fb_fd);
        execl(player_cmd, player_cmd, NULL);
        return 1;
    }

    /* Volume Up/Down select; Play/Pause or touch confirms. */
    int nav_fds[3] = {-1, -1, -1};
    const char *input_devices[] = {"/dev/input/event0", "/dev/input/event1", "/dev/input/event2"};
    for (int i = 0; i < 3; i++) {
        nav_fds[i] = open(input_devices[i], O_RDONLY | O_NONBLOCK);
    }

    time_t start_time = time(NULL);

    // Main loop
    while (1) {
        int elapsed = (int)(time(NULL) - start_time);
        int remaining = TIMEOUT_SEC - elapsed;

        if (remaining <= 0) {
            // Timeout - launch default
            break;
        }

        draw_boot_menu(selected, remaining);

        // Check touch input
        int tx, ty;
        if (read_touch(&tx, &ty)) {
            if (ty >= 320 && ty <= 420) {
                selected = 0; // Music Player
                break;
            } else if (ty >= 470 && ty <= 570) {
                selected = 1; // Game Boy
                break;
            }
        }

        // Hardware button navigation (KEY_UP/DOWN = volume, KEY_ENTER = play)
        struct input_event ev;
        for (int i = 0; i < 3; i++) {
            if (nav_fds[i] < 0) continue;
            while (read(nav_fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
                if (ev.type == EV_KEY && ev.value == 1) {
                    switch (ev.code) {
                        case KEY_UP:   selected = 0; break;
                        case KEY_DOWN: selected = 1; break;
                        case KEY_ENTER:
                        case KEY_PLAYPAUSE:
                            goto selected_done;
                    }
                }
            }
        }

        usleep(50000); // 50ms refresh
    }

selected_done:

    // Cleanup
    for (int i = 0; i < 3; i++) {
        if (nav_fds[i] >= 0) close(nav_fds[i]);
    }
    munmap(fb_mem, fb_size);
    close(fb_fd);

    if (selected == 1) {
        // Launch emulator
        printf("Launching Game Boy Emulator...\n");
        execl(emu_cmd, emu_cmd, NULL);
        // If emulator not found, fall through to player
        fprintf(stderr, "GB emulator not found at %s\n", emu_cmd);
    }

    // Default: launch music player
    printf("Launching Music Player...\n");
    execl(player_cmd, player_cmd, NULL);
    perror("execl");
    return 1;
}
