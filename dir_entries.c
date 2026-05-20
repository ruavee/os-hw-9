#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "ext2.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *program_name = "ext2_dir_entries";

static void usage(void) {
    fprintf(stderr, "usage: %s [directory-data-file]\n", program_name);
    fprintf(stderr, "       %s < directory-data-file\n", program_name);
}

static int append_bytes(unsigned char **buf, size_t *size, size_t *cap, const unsigned char *chunk, size_t n) {
    if (*size + n > *cap) {
        size_t new_cap = (*cap == 0U) ? 4096U : *cap;
        while (new_cap < *size + n) new_cap *= 2U;
        unsigned char *new_buf = realloc(*buf, new_cap);
        if (new_buf == NULL) return -1;
        *buf = new_buf;
        *cap = new_cap;
    }
    memcpy(*buf + *size, chunk, n);
    *size += n;
    return 0;
}

static int read_stream(FILE *fp, unsigned char **buf, size_t *size) {
    unsigned char tmp[8192];
    size_t cap = 0;
    *buf = NULL;
    *size = 0;
    for (;;) {
        size_t n = fread(tmp, 1, sizeof(tmp), fp);
        if (n > 0U && append_bytes(buf, size, &cap, tmp, n) == -1) return -1;
        if (n < sizeof(tmp)) {
            if (ferror(fp)) return -1;
            return 0;
        }
    }
}

static void print_escaped_name(const unsigned char *p, size_t n) {
    putchar('"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = p[i];
        if (c == '\\') fputs("\\\\", stdout);
        else if (c == '"') fputs("\\\"", stdout);
        else if (c >= 32U && c <= 126U) putchar((int)c);
        else printf("\\x%02x", c);
    }
    putchar('"');
}

static int parse_directory(const unsigned char *buf, size_t size) {
    size_t off = 0;
    while (off + 8U <= size) {
        uint32_t ino = ext2_get_le32(buf + off + 0U);
        uint16_t rec_len = ext2_get_le16(buf + off + 4U);
        uint8_t name_len = buf[off + 6U];
        uint8_t file_type = buf[off + 7U];

        if (rec_len < 8U || (rec_len % 4U) != 0U || off + rec_len > size) {
            fprintf(stderr, "%s: broken directory entry at offset %zu\n", program_name, off);
            return -1;
        }
        if ((size_t)name_len > (size_t)rec_len - 8U) {
            fprintf(stderr, "%s: name_len exceeds rec_len at offset %zu\n", program_name, off);
            return -1;
        }

        if (ino != 0U) {
            printf("offset=%zu inode=%" PRIu32 " rec_len=%" PRIu16 " name_len=%" PRIu8 " type=%" PRIu8 "(%s) name=", off, ino, rec_len, name_len, file_type, ext2_dirent_type_name(file_type));
            print_escaped_name(buf + off + 8U, name_len);
            putchar('\n');
        }
        off += rec_len;
    }
    if (off != size) {
        fprintf(stderr, "%s: trailing %zu byte(s) after last full entry\n", program_name, size - off);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    FILE *fp = stdin;
    unsigned char *buf = NULL;
    size_t size = 0;
    int rc;

    program_name = argv[0];
    if (argc > 2) {
        usage();
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        fp = fopen(argv[1], "rb");
        if (fp == NULL) {
            ext2_print_error(program_name, "cannot open directory data file");
            return EXIT_FAILURE;
        }
    }

    if (read_stream(fp, &buf, &size) == -1) {
        ext2_print_error(program_name, "cannot read directory data");
        if (fp != stdin) fclose(fp);
        free(buf);
        return EXIT_FAILURE;
    }
    if (fp != stdin && fclose(fp) == EOF) {
        ext2_print_error(program_name, "cannot close directory data file");
        free(buf);
        return EXIT_FAILURE;
    }

    rc = parse_directory(buf, size);
    free(buf);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
