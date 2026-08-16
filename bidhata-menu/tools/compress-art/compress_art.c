/* compress_art -- the JPEG diet plan. Shrinks oversized cover art instead of
 * nuking it (looking at you, strip_art). Think Marie Kondo, but for pixels.
 *
 * FLAC:  Type 6 METADATA_BLOCK_PICTURE too big? Decode, resize to fit 480x480,
 *        re-encode as JPEG, patch mime/width/height/depth/length. Every other
 *        block (STREAMINFO, VORBIS_COMMENT, etc.) untouched like a museum piece.
 * MP3:   Same but inside an ID3v2.3/2.4 APIC frame. Linked pictures ("-->")
 *        left alone -- nothing to shrink, like trying to compress a URL.
 * JPEG/PNG: Standalone files (folder.jpg, cover.png ...) resized in place,
 *        same format, never renamed. Already small enough? Don't touch. Too
 *        big to decode safely (MAX_DECODE_PIXELS)? Also don't touch -- OOM is
 *        not a vibe on 64 MB RAM.
 *
 * Every write: temp file + fsync + rename. Power loss mid-run? Original safe.
 * Atomic like a good transaction, not like a certain chemistry department.
 *
 * Usage: compress_art <file.flac|file.mp3|file.jpg|file.jpeg|file.png>
 * Exit: 0 compressed, 1 nothing to do, 2 error/skip. Yes, 3 exit codes. We're fancy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#define STBI_NO_TGA
#define STBI_NO_PSD
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define COPY_BUF 65536

/* Max side we keep: 480, matching the R1's screen. Bigger than screen = wasted bytes, like buying jeans two sizes up. */
#define MAX_DIM 480
#define JPEG_QUALITY 85 /* 85% JPEG -- lossy enough to save space, not enough to make your album art look like Minecraft. */

/* Don't decode above ~7.8M pixels on-device -- 64 MB RAM is not infinite. Bigger? Skip, stay safe. */
#define MAX_DECODE_PIXELS (2800L * 2800L)

/* ------------------------------------------------------------ shared I/O */

/* Bytes remaining to EOF without moving the file pointer. Sneaky peek. */
static long remaining_bytes(FILE *in) {
    long cur = ftell(in);
    fseek(in, 0, SEEK_END);
    long end = ftell(in);
    fseek(in, cur, SEEK_SET);
    return end - cur;
}

static int copy_stream(FILE *in, FILE *out, long n) {
    unsigned char buf[COPY_BUF];
    while (n > 0) {
        long chunk = n > (long)sizeof(buf) ? (long)sizeof(buf) : n;
        if (fread(buf, 1, (size_t)chunk, in) != (size_t)chunk) return -1;
        if (fwrite(buf, 1, (size_t)chunk, out) != (size_t)chunk) return -1;
        n -= chunk;
    }
    return 0;
}

/* Atomic replace: write to .compress_tmp, fsync, rename. Crash-safe like a journaling filesystem's cousin. */
static int atomic_replace(const char *path, int (*fill)(FILE *in, FILE *tmp, void *ctx), void *ctx, FILE *in) {
    char tmp_path[4096];
    snprintf(tmp_path, sizeof(tmp_path), "%s.compress_tmp", path);

    FILE *tmp = fopen(tmp_path, "wb");
    if (!tmp) {
        fprintf(stderr, "compress_art: cannot create %s: %s\n", tmp_path, strerror(errno));
        return -1;
    }

    if (fill(in, tmp, ctx) != 0) {
        fprintf(stderr, "compress_art: write failed for %s\n", tmp_path);
        fclose(tmp);
        unlink(tmp_path);
        return -1;
    }

    if (fflush(tmp) != 0 || fsync(fileno(tmp)) != 0) {
        fprintf(stderr, "compress_art: fsync failed for %s: %s\n", tmp_path, strerror(errno));
        fclose(tmp);
        unlink(tmp_path);
        return -1;
    }
    fclose(tmp);

    if (rename(tmp_path, path) != 0) {
        fprintf(stderr, "compress_art: rename failed: %s\n", strerror(errno));
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

/* Same atomic dance but for a malloc'd buffer (standalone image path has no streaming source). */
static int atomic_replace_buf(const char *path, const unsigned char *data, size_t len) {
    char tmp_path[4096];
    snprintf(tmp_path, sizeof(tmp_path), "%s.compress_tmp", path);

    FILE *tmp = fopen(tmp_path, "wb");
    if (!tmp) {
        fprintf(stderr, "compress_art: cannot create %s: %s\n", tmp_path, strerror(errno));
        return -1;
    }
    size_t wrote = fwrite(data, 1, len, tmp);
    if (wrote != len || fflush(tmp) != 0 || fsync(fileno(tmp)) != 0) {
        fprintf(stderr, "compress_art: write failed for %s\n", tmp_path);
        fclose(tmp);
        unlink(tmp_path);
        return -1;
    }
    fclose(tmp);

    if (rename(tmp_path, path) != 0) {
        fprintf(stderr, "compress_art: rename failed: %s\n", strerror(errno));
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

/* --------------------------------------------------------- image shrink */

typedef struct { unsigned char *data; size_t len, cap; } mem_buf_t;

static void mem_write_cb(void *context, void *data, int size) {
    mem_buf_t *mb = (mem_buf_t *)context;
    if (size <= 0) return;
    if (mb->len + (size_t)size > mb->cap) {
        size_t newcap = mb->cap ? mb->cap * 2 : 65536;
        while (newcap < mb->len + (size_t)size) newcap *= 2;
        unsigned char *grown = realloc(mb->data, newcap);
        if (!grown) return; /* leaves mb->data as-is; caller checks mb->len vs expected */
        mb->data = grown;
        mb->cap = newcap;
    }
    memcpy(mb->data + mb->len, data, (size_t)size);
    mb->len += (size_t)size;
}

/* Fit src into max_dim box, preserve aspect. "Contain" not "cover" -- we don't crop art like a bad Instagram. */
static void fit_dims(int src_w, int src_h, int max_dim, int *dst_w, int *dst_h) {
    if (src_w >= src_h) {
        *dst_w = max_dim;
        *dst_h = (int)((int64_t)src_h * max_dim / src_w);
        if (*dst_h < 1) *dst_h = 1;
    } else {
        *dst_h = max_dim;
        *dst_w = (int)((int64_t)src_w * max_dim / src_h);
        if (*dst_w < 1) *dst_w = 1;
    }
}

/* Try to shrink: decode, check if >MAX_DIM, resize, re-encode as JPEG.
 * Returns false = "leave it alone" if already small / undecodable / too huge. */
static bool shrink_picture(const unsigned char *in, uint32_t in_len,
                            unsigned char **out_data, uint32_t *out_size,
                            int *out_w, int *out_h) {
    int w, h, comp;
    if (!stbi_info_from_memory(in, (int)in_len, &w, &h, &comp)) return false;
    if (w <= MAX_DIM && h <= MAX_DIM) return false;
    if ((long)w * (long)h > MAX_DECODE_PIXELS) {
        fprintf(stderr, "compress_art: picture %dx%d exceeds safe on-device decode size, leaving as-is\n", w, h);
        return false;
    }

    int src_w, src_h;
    unsigned char *pixels = stbi_load_from_memory(in, (int)in_len, &src_w, &src_h, &comp, 3);
    if (!pixels) return false;

    int dst_w, dst_h;
    fit_dims(src_w, src_h, MAX_DIM, &dst_w, &dst_h);

    unsigned char *resized = malloc((size_t)dst_w * (size_t)dst_h * 3);
    if (!resized) {
        stbi_image_free(pixels);
        return false;
    }
    stbir_resize_uint8_srgb(pixels, src_w, src_h, 0, resized, dst_w, dst_h, 0, STBIR_RGB);
    stbi_image_free(pixels);

    mem_buf_t mb = {0};
    int ok = stbi_write_jpg_to_func(mem_write_cb, &mb, dst_w, dst_h, 3, resized, JPEG_QUALITY);
    free(resized);
    if (!ok || !mb.data) {
        free(mb.data);
        return false;
    }

    *out_data = mb.data;
    *out_size = (uint32_t)mb.len;
    *out_w = dst_w;
    *out_h = dst_h;
    return true;
}

/* ------------------------------------------------------------------ FLAC */

typedef struct {
    unsigned char type;
    uint32_t length;
    unsigned char *data;
} flac_block_t;

static int flac_fill(FILE *in, FILE *tmp, void *vctx) {
    typedef struct { flac_block_t *blocks; int count; long audio_offset; } flac_ctx_t;
    flac_ctx_t *ctx = (flac_ctx_t *)vctx;

    if (fwrite("fLaC", 1, 4, tmp) != 4) return -1;

    for (int i = 0; i < ctx->count; i++) {
        flac_block_t *b = &ctx->blocks[i];
        unsigned char hdr[4];
        hdr[0] = (unsigned char)((i == ctx->count - 1 ? 0x80 : 0x00) | (b->type & 0x7F));
        hdr[1] = (unsigned char)((b->length >> 16) & 0xFF);
        hdr[2] = (unsigned char)((b->length >> 8) & 0xFF);
        hdr[3] = (unsigned char)(b->length & 0xFF);
        if (fwrite(hdr, 1, 4, tmp) != 4) return -1;
        if (b->length > 0 && fwrite(b->data, 1, b->length, tmp) != b->length) return -1;
    }

    if (fseek(in, ctx->audio_offset, SEEK_SET) != 0) return -1;
    return copy_stream(in, tmp, remaining_bytes(in));
}

static unsigned char *build_flac_picture(uint32_t picture_type,
                                          const char *mime, uint32_t mime_len,
                                          const unsigned char *desc, uint32_t desc_len,
                                          uint32_t width, uint32_t height,
                                          uint32_t depth, uint32_t colors,
                                          const unsigned char *img, uint32_t img_len,
                                          uint32_t *out_len) {
    uint32_t total = 4 + (4 + mime_len) + (4 + desc_len) + 4 + 4 + 4 + 4 + (4 + img_len);
    unsigned char *buf = malloc(total);
    if (!buf) return NULL;
    unsigned char *p = buf;

#define PUT32(v) do { uint32_t _v = (v); *p++ = (unsigned char)(_v >> 24); *p++ = (unsigned char)(_v >> 16); \
                       *p++ = (unsigned char)(_v >> 8); *p++ = (unsigned char)_v; } while (0)
    PUT32(picture_type);
    PUT32(mime_len);
    memcpy(p, mime, mime_len); p += mime_len;
    PUT32(desc_len);
    if (desc_len) memcpy(p, desc, desc_len);
    p += desc_len;
    PUT32(width);
    PUT32(height);
    PUT32(depth);
    PUT32(colors);
    PUT32(img_len);
    memcpy(p, img, img_len); p += img_len;
#undef PUT32

    *out_len = total;
    return buf;
}

static bool rd32(const unsigned char *raw, uint32_t raw_len, uint32_t *off, uint32_t *val) {
    if (*off + 4 > raw_len) return false;
    *val = ((uint32_t)raw[*off] << 24) | ((uint32_t)raw[*off + 1] << 16) |
           ((uint32_t)raw[*off + 2] << 8) | raw[*off + 3];
    *off += 4;
    return true;
}

/* Parse raw METADATA_BLOCK_PICTURE. Any truncation/malformed? Abort, leave untouched. Hippocratic Oath for bits. */
static bool parse_flac_picture(const unsigned char *raw, uint32_t raw_len,
                                uint32_t *out_type,
                                uint32_t *out_width, uint32_t *out_height,
                                uint32_t *desc_off, uint32_t *desc_len,
                                uint32_t *img_off, uint32_t *img_len) {
    uint32_t off = 0, v, depth, colors;
    if (!rd32(raw, raw_len, &off, out_type)) return false;
    if (!rd32(raw, raw_len, &off, &v)) return false;          /* mime length */
    if (off + v > raw_len) return false;
    off += v;                                                  /* mime bytes: unused, always rewritten */
    if (!rd32(raw, raw_len, &off, &v)) return false;          /* description length */
    if (off + v > raw_len) return false;
    *desc_off = off; *desc_len = v;
    off += v;
    if (!rd32(raw, raw_len, &off, out_width)) return false;
    if (!rd32(raw, raw_len, &off, out_height)) return false;
    if (!rd32(raw, raw_len, &off, &depth)) return false;
    if (!rd32(raw, raw_len, &off, &colors)) return false;
    if (!rd32(raw, raw_len, &off, &v)) return false;          /* data length */
    if (off + v > raw_len) return false;
    *img_off = off; *img_len = v;
    return true;
}

static int compress_flac(const char *path) {
    FILE *in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "compress_art: cannot open %s: %s\n", path, strerror(errno));
        return 2;
    }

    unsigned char magic[4];
    if (fread(magic, 1, 4, in) != 4 || memcmp(magic, "fLaC", 4) != 0) {
        fprintf(stderr, "compress_art: %s is not a FLAC file\n", path);
        fclose(in);
        return 2;
    }

    flac_block_t kept[256];
    int kept_count = 0;
    int changed = 0;
    int is_last = 0;

    while (!is_last) {
        unsigned char hdr[4];
        if (fread(hdr, 1, 4, in) != 4) {
            fprintf(stderr, "compress_art: %s: truncated metadata block header\n", path);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }
        is_last = (hdr[0] & 0x80) != 0;
        unsigned char type = hdr[0] & 0x7F;
        uint32_t length = ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];

        if (kept_count >= (int)(sizeof(kept) / sizeof(kept[0]))) {
            fprintf(stderr, "compress_art: %s: too many metadata blocks, skipping\n", path);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }

        unsigned char *raw = length ? malloc(length) : NULL;
        if (length && !raw) {
            fprintf(stderr, "compress_art: %s: out of memory (block %u bytes)\n", path, length);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }
        if (length && fread(raw, 1, length, in) != length) {
            fprintf(stderr, "compress_art: %s: truncated metadata block body\n", path);
            free(raw);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }

        if (type == 6) {
            uint32_t p_type, width, height, desc_off, desc_len, img_off, img_len;
            bool parsed = parse_flac_picture(raw, length, &p_type, &width, &height,
                                              &desc_off, &desc_len, &img_off, &img_len);
            unsigned char *new_block = NULL;
            uint32_t new_block_len = 0;

            if (parsed) {
                unsigned char *jpg = NULL;
                uint32_t jpg_len = 0;
                int nw, nh;
                if (shrink_picture(raw + img_off, img_len, &jpg, &jpg_len, &nw, &nh)) {
                    new_block = build_flac_picture(p_type, "image/jpeg", 10,
                                                    raw + desc_off, desc_len,
                                                    (uint32_t)nw, (uint32_t)nh, 24, 0,
                                                    jpg, jpg_len, &new_block_len);
                    free(jpg);
                    if (new_block) changed = 1;
                }
            }

            kept[kept_count].type = 6;
            if (new_block) {
                kept[kept_count].length = new_block_len;
                kept[kept_count].data = new_block;
                free(raw);
            } else {
                kept[kept_count].length = length;
                kept[kept_count].data = raw;
            }
        } else {
            kept[kept_count].type = type;
            kept[kept_count].length = length;
            kept[kept_count].data = raw;
        }
        kept_count++;
    }

    if (!changed) {
        fclose(in);
        for (int i = 0; i < kept_count; i++) free(kept[i].data);
        return 1; /* nothing to do -- no picture, or all already small enough */
    }

    typedef struct { flac_block_t *blocks; int count; long audio_offset; } flac_ctx_t;
    flac_ctx_t ctx;
    ctx.blocks = kept;
    ctx.count = kept_count;
    ctx.audio_offset = ftell(in);

    int rc = atomic_replace(path, flac_fill, &ctx, in);
    fclose(in);
    for (int i = 0; i < kept_count; i++) free(kept[i].data);
    return rc == 0 ? 0 : 2;
}

/* -------------------------------------------------------------- ID3v2/MP3 */

typedef struct {
    unsigned char id[4];
    unsigned char flags[2];
    uint32_t size;
    unsigned char *data;
} id3_frame_t;

typedef struct {
    unsigned char version_major;
    unsigned char header_flags;
    id3_frame_t *frames;
    int count;
    long audio_offset;
} id3_ctx_t;

static uint32_t syncsafe_decode(const unsigned char b[4]) {
    return ((uint32_t)(b[0] & 0x7F) << 21) | ((uint32_t)(b[1] & 0x7F) << 14) |
           ((uint32_t)(b[2] & 0x7F) << 7) | (uint32_t)(b[3] & 0x7F);
}

static void syncsafe_encode(uint32_t v, unsigned char out[4]) {
    out[0] = (unsigned char)((v >> 21) & 0x7F);
    out[1] = (unsigned char)((v >> 14) & 0x7F);
    out[2] = (unsigned char)((v >> 7) & 0x7F);
    out[3] = (unsigned char)(v & 0x7F);
}

static int id3_fill(FILE *in, FILE *tmp, void *vctx) {
    id3_ctx_t *ctx = (id3_ctx_t *)vctx;

    uint32_t new_tag_size = 0;
    for (int i = 0; i < ctx->count; i++) {
        new_tag_size += 10 + ctx->frames[i].size;
    }

    unsigned char hdr[10];
    hdr[0] = 'I'; hdr[1] = 'D'; hdr[2] = '3';
    hdr[3] = ctx->version_major;
    hdr[4] = 0;
    hdr[5] = ctx->header_flags;
    syncsafe_encode(new_tag_size, hdr + 6);
    if (fwrite(hdr, 1, 10, tmp) != 10) return -1;

    for (int i = 0; i < ctx->count; i++) {
        id3_frame_t *f = &ctx->frames[i];
        unsigned char fhdr[10];
        memcpy(fhdr, f->id, 4);
        if (ctx->version_major == 4) {
            syncsafe_encode(f->size, fhdr + 4);
        } else {
            fhdr[4] = (unsigned char)((f->size >> 24) & 0xFF);
            fhdr[5] = (unsigned char)((f->size >> 16) & 0xFF);
            fhdr[6] = (unsigned char)((f->size >> 8) & 0xFF);
            fhdr[7] = (unsigned char)(f->size & 0xFF);
        }
        memcpy(fhdr + 8, f->flags, 2);
        if (fwrite(fhdr, 1, 10, tmp) != 10) return -1;
        if (f->size > 0 && fwrite(f->data, 1, f->size, tmp) != f->size) return -1;
    }

    if (fseek(in, ctx->audio_offset, SEEK_SET) != 0) return -1;
    return copy_stream(in, tmp, remaining_bytes(in));
}

/* Rebuild APIC body: keep encoding+description verbatim, swap mime to image/jpeg, swap picture bytes. ID3v2 quirk: mime is always Latin-1 NUL-terminated regardless of encoding. */
static unsigned char *build_apic_body(unsigned char encoding, unsigned char pic_type,
                                       const unsigned char *desc, uint32_t desc_len,
                                       const unsigned char *img, uint32_t img_len,
                                       uint32_t *out_len) {
    static const char MIME[] = "image/jpeg";
    uint32_t mime_len = (uint32_t)(sizeof(MIME) - 1);
    uint32_t total = 1 + mime_len + 1 + 1 + desc_len + img_len;
    unsigned char *buf = malloc(total);
    if (!buf) return NULL;
    unsigned char *p = buf;
    *p++ = encoding;
    memcpy(p, MIME, mime_len); p += mime_len;
    *p++ = 0;
    *p++ = pic_type;
    if (desc_len) memcpy(p, desc, desc_len);
    p += desc_len;
    if (img_len) memcpy(p, img, img_len);
    p += img_len;
    *out_len = total;
    return buf;
}

/* Parse APIC body. Linked picture ("-->")? Nothing to shrink, bye. Truncated? Also bye. */
static bool parse_id3_apic(const unsigned char *body, uint32_t body_len,
                            unsigned char *out_encoding, unsigned char *out_pic_type,
                            uint32_t *desc_off, uint32_t *desc_len,
                            uint32_t *img_off, uint32_t *img_len) {
    if (body_len < 1) return false;
    unsigned char encoding = body[0];
    uint32_t off = 1;

    uint32_t mime_start = off;
    while (off < body_len && body[off] != 0) off++;
    if (off >= body_len) return false;
    uint32_t mime_len = off - mime_start;
    off++; /* skip NUL */

    if (mime_len == 3 && memcmp(body + mime_start, "-->", 3) == 0) return false; /* linked picture */

    if (off >= body_len) return false;
    unsigned char pic_type = body[off++];

    uint32_t d_start = off;
    if (encoding == 1 || encoding == 2) {
        while (off + 1 < body_len && !(body[off] == 0 && body[off + 1] == 0)) off += 2;
        if (off + 1 >= body_len) return false;
        off += 2;
    } else {
        while (off < body_len && body[off] != 0) off++;
        if (off >= body_len) return false;
        off++;
    }

    *out_encoding = encoding;
    *out_pic_type = pic_type;
    *desc_off = d_start;
    *desc_len = off - d_start;
    *img_off = off;
    *img_len = body_len - off;
    return true;
}

static int compress_mp3(const char *path) {
    FILE *in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "compress_art: cannot open %s: %s\n", path, strerror(errno));
        return 2;
    }

    unsigned char hdr[10];
    if (fread(hdr, 1, 10, in) != 10 || memcmp(hdr, "ID3", 3) != 0) {
        fclose(in);
        return 1; /* no ID3v2 tag -- nothing this tool can find to shrink */
    }

    unsigned char version_major = hdr[3];
    unsigned char flags = hdr[5];
    uint32_t tag_size = syncsafe_decode(hdr + 6);

    if (version_major != 3 && version_major != 4) {
        fprintf(stderr, "compress_art: %s: ID3v2.%d unsupported, skipping\n", path, version_major);
        fclose(in);
        return 2;
    }
    if (flags & 0x40) {
        fprintf(stderr, "compress_art: %s: extended ID3 header present, skipping to be safe\n", path);
        fclose(in);
        return 2;
    }

    id3_frame_t kept[512];
    int kept_count = 0;
    int changed = 0;
    uint32_t consumed = 0;

    while (consumed + 10 <= tag_size) {
        unsigned char fhdr[10];
        if (fread(fhdr, 1, 10, in) != 10) break;
        if (fhdr[0] == 0) break; /* padding reached */

        uint32_t fsize = (version_major == 4)
            ? syncsafe_decode(fhdr + 4)
            : ((uint32_t)fhdr[4] << 24 | (uint32_t)fhdr[5] << 16 |
               (uint32_t)fhdr[6] << 8 | (uint32_t)fhdr[7]);

        consumed += 10;
        if (consumed + fsize > tag_size) {
            fprintf(stderr, "compress_art: %s: frame size runs past tag, skipping file\n", path);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }

        if (kept_count >= (int)(sizeof(kept) / sizeof(kept[0]))) {
            fprintf(stderr, "compress_art: %s: too many frames, skipping\n", path);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }

        unsigned char *fdata = fsize ? malloc(fsize) : NULL;
        if (fsize && !fdata) {
            fprintf(stderr, "compress_art: %s: out of memory (frame %u bytes)\n", path, fsize);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }
        if (fsize && fread(fdata, 1, fsize, in) != fsize) {
            fprintf(stderr, "compress_art: %s: truncated frame body\n", path);
            free(fdata);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }

        int is_apic = memcmp(fhdr, "APIC", 4) == 0;
        unsigned char *new_body = NULL;
        uint32_t new_body_len = 0;

        if (is_apic) {
            unsigned char encoding, pic_type;
            uint32_t desc_off, desc_len, img_off, img_len;
            if (parse_id3_apic(fdata, fsize, &encoding, &pic_type, &desc_off, &desc_len, &img_off, &img_len)) {
                unsigned char *jpg = NULL;
                uint32_t jpg_len = 0;
                int nw, nh;
                if (shrink_picture(fdata + img_off, img_len, &jpg, &jpg_len, &nw, &nh)) {
                    new_body = build_apic_body(encoding, pic_type, fdata + desc_off, desc_len,
                                                jpg, jpg_len, &new_body_len);
                    free(jpg);
                    if (new_body) changed = 1;
                }
            }
        }

        memcpy(kept[kept_count].id, fhdr, 4);
        memcpy(kept[kept_count].flags, fhdr + 8, 2);
        if (new_body) {
            kept[kept_count].size = new_body_len;
            kept[kept_count].data = new_body;
            free(fdata);
        } else {
            kept[kept_count].size = fsize;
            kept[kept_count].data = fdata;
        }
        kept_count++;
        consumed += fsize;
    }

    if (!changed) {
        fclose(in);
        for (int i = 0; i < kept_count; i++) free(kept[i].data);
        return 1;
    }

    id3_ctx_t ctx;
    ctx.version_major = version_major;
    ctx.header_flags = flags;
    ctx.frames = kept;
    ctx.count = kept_count;
    ctx.audio_offset = 10 + (long)tag_size;

    int rc = atomic_replace(path, id3_fill, &ctx, in);
    fclose(in);
    for (int i = 0; i < kept_count; i++) free(kept[i].data);
    return rc == 0 ? 0 : 2;
}

/* ------------------------------------------------------------- standalone */

static int compress_image_file(const char *path, int is_png) {
    FILE *in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "compress_art: cannot open %s: %s\n", path, strerror(errno));
        return 2;
    }
    long sz = remaining_bytes(in);
    if (sz <= 0) {
        fclose(in);
        return 2;
    }
    unsigned char *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(in);
        return 2;
    }
    if (fread(buf, 1, (size_t)sz, in) != (size_t)sz) {
        fprintf(stderr, "compress_art: %s: read failed\n", path);
        free(buf);
        fclose(in);
        return 2;
    }
    fclose(in);

    unsigned char *jpg = NULL; /* actually JPEG or PNG bytes depending on is_png */
    uint32_t jpg_len = 0;
    int nw, nh;

    int w, h, comp;
    if (!stbi_info_from_memory(buf, (int)sz, &w, &h, &comp)) {
        fprintf(stderr, "compress_art: %s: not a decodable image, skipping\n", path);
        free(buf);
        return 2;
    }
    if (w <= MAX_DIM && h <= MAX_DIM) {
        free(buf);
        return 1;
    }
    if ((long)w * (long)h > MAX_DECODE_PIXELS) {
        fprintf(stderr, "compress_art: %s: %dx%d exceeds safe on-device decode size, skipping\n", path, w, h);
        free(buf);
        return 2;
    }

    int src_w, src_h;
    unsigned char *pixels = stbi_load_from_memory(buf, (int)sz, &src_w, &src_h, &comp, 3);
    free(buf);
    if (!pixels) {
        fprintf(stderr, "compress_art: %s: decode failed, skipping\n", path);
        return 2;
    }

    fit_dims(src_w, src_h, MAX_DIM, &nw, &nh);
    unsigned char *resized = malloc((size_t)nw * (size_t)nh * 3);
    if (!resized) {
        stbi_image_free(pixels);
        return 2;
    }
    stbir_resize_uint8_srgb(pixels, src_w, src_h, 0, resized, nw, nh, 0, STBIR_RGB);
    stbi_image_free(pixels);

    mem_buf_t mb = {0};
    int ok = is_png
        ? stbi_write_png_to_func(mem_write_cb, &mb, nw, nh, 3, resized, nw * 3)
        : stbi_write_jpg_to_func(mem_write_cb, &mb, nw, nh, 3, resized, JPEG_QUALITY);
    free(resized);
    if (!ok || !mb.data) {
        free(mb.data);
        return 2;
    }
    jpg = mb.data;
    jpg_len = (uint32_t)mb.len;

    int rc = atomic_replace_buf(path, jpg, jpg_len);
    free(jpg);
    return rc == 0 ? 0 : 2;
}

/* ------------------------------------------------------------------ main */

static int has_ext(const char *path, const char *ext) {
    size_t plen = strlen(path), elen = strlen(ext);
    if (plen <= elen) return 0;
    for (size_t i = 0; i < elen; i++) {
        char a = path[plen - elen + i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (a != b) return 0;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.flac|file.mp3|file.jpg|file.jpeg|file.png>\n", argv[0]);
        return 2;
    }

    const char *path = argv[1];
    int rc;
    if (has_ext(path, ".flac")) {
        rc = compress_flac(path);
    } else if (has_ext(path, ".mp3")) {
        rc = compress_mp3(path);
    } else if (has_ext(path, ".jpg") || has_ext(path, ".jpeg")) {
        rc = compress_image_file(path, 0);
    } else if (has_ext(path, ".png")) {
        rc = compress_image_file(path, 1);
    } else if (has_ext(path, ".bmp")) {
        fprintf(stderr, "compress_art: %s: BMP not supported, skipping\n", path);
        return 2;
    } else {
        fprintf(stderr, "compress_art: %s: unrecognized extension\n", path);
        return 2;
    }

    if (rc == 0) printf("compressed: %s\n", path);
    return rc;
}
