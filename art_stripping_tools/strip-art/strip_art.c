/* strip_art: removes embedded cover art from a FLAC or MP3 file in place.
 *
 * FLAC:  drops METADATA_BLOCK_PICTURE (type 6) blocks from the metadata
 *        chain, keeping everything else (STREAMINFO, VORBIS_COMMENT,
 *        SEEKTABLE, CUESHEET, PADDING, ...) byte-for-byte and re-flagging
 *        whichever kept block is now last.
 * MP3:   drops APIC frames from an ID3v2.3/2.4 tag, keeping every other
 *        frame's header and body untouched and shrinking the tag's declared
 *        size to match. Skips (does not modify) tags with an extended
 *        header or any version this doesn't recognize, rather than risk
 *        misparsing one.
 *
 * Every write goes to a temp file in the same directory first, fsync'd,
 * then renamed over the original -- a crash or power loss mid-run leaves
 * the original file exactly as it was, never half-written.
 *
 * A file with no embedded picture is left untouched: nothing is opened for
 * writing, so there's no needless wear on the SD card re-writing files that
 * were already fine.
 *
 * Usage: strip_art <file.flac|file.mp3>
 * Exit status: 0 stripped, 1 nothing to do (no picture found), 2 error/skip.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define COPY_BUF 65536

typedef struct {
    unsigned char type;
    uint32_t length;
    unsigned char *data;
} flac_block_t;

/* Bytes left to read in `in` from its current position to EOF, leaving the
 * position unchanged. Avoids a stat()/fstat() dependency, which differs
 * enough between the glibc host toolchain and the musl device one that it's
 * simpler to just measure it via the stream itself. */
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

/* Writes to a temp file beside `path`, then atomically renames it over
 * `path`. `fill` does the actual writing to `tmp`. Returns 0 on success. */
static int atomic_replace(const char *path, int (*fill)(FILE *in, FILE *tmp, void *ctx), void *ctx, FILE *in) {
    char tmp_path[4096];
    snprintf(tmp_path, sizeof(tmp_path), "%s.strip_tmp", path);

    FILE *tmp = fopen(tmp_path, "wb");
    if (!tmp) {
        fprintf(stderr, "strip_art: cannot create %s: %s\n", tmp_path, strerror(errno));
        return -1;
    }

    if (fill(in, tmp, ctx) != 0) {
        fprintf(stderr, "strip_art: write failed for %s\n", tmp_path);
        fclose(tmp);
        unlink(tmp_path);
        return -1;
    }

    if (fflush(tmp) != 0 || fsync(fileno(tmp)) != 0) {
        fprintf(stderr, "strip_art: fsync failed for %s: %s\n", tmp_path, strerror(errno));
        fclose(tmp);
        unlink(tmp_path);
        return -1;
    }
    fclose(tmp);

    if (rename(tmp_path, path) != 0) {
        fprintf(stderr, "strip_art: rename failed: %s\n", strerror(errno));
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ FLAC */

typedef struct {
    flac_block_t *blocks;
    int count;
    long audio_offset; /* file offset where non-metadata audio data starts */
} flac_ctx_t;

static int flac_fill(FILE *in, FILE *tmp, void *vctx) {
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

static int strip_flac(const char *path) {
    FILE *in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "strip_art: cannot open %s: %s\n", path, strerror(errno));
        return 2;
    }

    unsigned char magic[4];
    if (fread(magic, 1, 4, in) != 4 || memcmp(magic, "fLaC", 4) != 0) {
        fprintf(stderr, "strip_art: %s is not a FLAC file\n", path);
        fclose(in);
        return 2;
    }

    flac_block_t kept[256];
    int kept_count = 0;
    int found_picture = 0;
    int is_last = 0;

    while (!is_last) {
        unsigned char hdr[4];
        if (fread(hdr, 1, 4, in) != 4) {
            fprintf(stderr, "strip_art: %s: truncated metadata block header\n", path);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }
        is_last = (hdr[0] & 0x80) != 0;
        unsigned char type = hdr[0] & 0x7F;
        uint32_t length = ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];

        if (type == 6) { /* PICTURE */
            found_picture = 1;
            if (fseek(in, (long)length, SEEK_CUR) != 0) {
                fprintf(stderr, "strip_art: %s: seek past PICTURE block failed\n", path);
                fclose(in);
                for (int i = 0; i < kept_count; i++) free(kept[i].data);
                return 2;
            }
            continue;
        }

        if (kept_count >= (int)(sizeof(kept) / sizeof(kept[0]))) {
            fprintf(stderr, "strip_art: %s: too many metadata blocks, skipping\n", path);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }

        unsigned char *data = length ? malloc(length) : NULL;
        if (length && !data) {
            fprintf(stderr, "strip_art: %s: out of memory (block %u bytes)\n", path, length);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }
        if (length && fread(data, 1, length, in) != length) {
            fprintf(stderr, "strip_art: %s: truncated metadata block body\n", path);
            free(data);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }

        kept[kept_count].type = type;
        kept[kept_count].length = length;
        kept[kept_count].data = data;
        kept_count++;
    }

    if (!found_picture) {
        fclose(in);
        for (int i = 0; i < kept_count; i++) free(kept[i].data);
        return 1; /* nothing to do */
    }

    if (kept_count == 0) {
        /* Should not happen -- STREAMINFO is mandatory and is never type 6 --
         * but refuse to write a metadata-less FLAC rather than guess. */
        fprintf(stderr, "strip_art: %s: no metadata blocks survive, skipping\n", path);
        fclose(in);
        return 2;
    }

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
    long audio_offset; /* file offset right after the original tag */
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
    hdr[4] = 0; /* revision, doesn't matter to any reader */
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

static int strip_mp3(const char *path) {
    FILE *in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "strip_art: cannot open %s: %s\n", path, strerror(errno));
        return 2;
    }

    unsigned char hdr[10];
    if (fread(hdr, 1, 10, in) != 10 || memcmp(hdr, "ID3", 3) != 0) {
        /* No ID3v2 tag at all -- nothing this tool can find to strip. */
        fclose(in);
        return 1;
    }

    unsigned char version_major = hdr[3];
    unsigned char flags = hdr[5];
    uint32_t tag_size = syncsafe_decode(hdr + 6);

    if (version_major != 3 && version_major != 4) {
        fprintf(stderr, "strip_art: %s: ID3v2.%d unsupported, skipping\n", path, version_major);
        fclose(in);
        return 2;
    }
    if (flags & 0x40) {
        fprintf(stderr, "strip_art: %s: extended ID3 header present, skipping to be safe\n", path);
        fclose(in);
        return 2;
    }

    id3_frame_t kept[512];
    int kept_count = 0;
    int found_picture = 0;
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
            fprintf(stderr, "strip_art: %s: frame size runs past tag, skipping file\n", path);
            fclose(in);
            for (int i = 0; i < kept_count; i++) free(kept[i].data);
            return 2;
        }

        int is_apic = memcmp(fhdr, "APIC", 4) == 0;
        if (is_apic) {
            found_picture = 1;
            if (fseek(in, (long)fsize, SEEK_CUR) != 0) {
                fclose(in);
                for (int i = 0; i < kept_count; i++) free(kept[i].data);
                return 2;
            }
        } else {
            if (kept_count >= (int)(sizeof(kept) / sizeof(kept[0]))) {
                fprintf(stderr, "strip_art: %s: too many frames, skipping\n", path);
                fclose(in);
                for (int i = 0; i < kept_count; i++) free(kept[i].data);
                return 2;
            }
            unsigned char *data = fsize ? malloc(fsize) : NULL;
            if (fsize && !data) {
                fprintf(stderr, "strip_art: %s: out of memory (frame %u bytes)\n", path, fsize);
                fclose(in);
                for (int i = 0; i < kept_count; i++) free(kept[i].data);
                return 2;
            }
            if (fsize && fread(data, 1, fsize, in) != fsize) {
                fprintf(stderr, "strip_art: %s: truncated frame body\n", path);
                free(data);
                fclose(in);
                for (int i = 0; i < kept_count; i++) free(kept[i].data);
                return 2;
            }
            memcpy(kept[kept_count].id, fhdr, 4);
            memcpy(kept[kept_count].flags, fhdr + 8, 2);
            kept[kept_count].size = fsize;
            kept[kept_count].data = data;
            kept_count++;
        }
        consumed += fsize;
    }

    if (!found_picture) {
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
        fprintf(stderr, "Usage: %s <file.flac|file.mp3>\n", argv[0]);
        return 2;
    }

    const char *path = argv[1];
    int rc;
    if (has_ext(path, ".flac")) {
        rc = strip_flac(path);
    } else if (has_ext(path, ".mp3")) {
        rc = strip_mp3(path);
    } else {
        fprintf(stderr, "strip_art: %s: unrecognized extension\n", path);
        return 2;
    }

    if (rc == 0) printf("stripped: %s\n", path);
    return rc;
}
