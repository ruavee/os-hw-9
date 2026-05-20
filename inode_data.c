#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "ext2.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static const char *program_name = "ext2_cat_inode";

static void usage(void) {
    fprintf(stderr, "usage: %s <ext2-image-or-block-device> <inode-number>\n", program_name);
}

static int parse_inode_number(const char *s, uint32_t *ino) {
    char *end = NULL;
    unsigned long v;
    errno = 0;
    v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v == 0UL || v > UINT32_MAX) return -1;
    *ino = (uint32_t)v;
    return 0;
}

int main(int argc, char **argv) {
    ext2_fs_t fs;
    ext2_inode_t inode;
    uint32_t ino;

    program_name = argv[0];
    if (argc != 3) {
        usage();
        return EXIT_FAILURE;
    }
    if (parse_inode_number(argv[2], &ino) == -1) {
        fprintf(stderr, "%s: invalid inode number: %s\n", program_name, argv[2]);
        return EXIT_FAILURE;
    }
    if (ext2_open(&fs, argv[1]) == -1) {
        ext2_print_error(program_name, "cannot open or parse ext2 filesystem");
        return EXIT_FAILURE;
    }
    if (ext2_read_inode(&fs, ino, &inode) == -1) {
        ext2_print_error(program_name, "cannot read inode");
        ext2_close(&fs);
        return EXIT_FAILURE;
    }
    if (ext2_write_inode_data_to_fd(&fs, &inode, 1) == -1) {
        ext2_print_error(program_name, "cannot write inode data");
        ext2_close(&fs);
        return EXIT_FAILURE;
    }
    ext2_close(&fs);
    return EXIT_SUCCESS;
}
