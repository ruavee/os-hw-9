#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "ext2.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *program_name = "ext2_inode_info";

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

static void print_time_field(const char *name, uint32_t value) {
    time_t t = (time_t)value;
    char buf[64];
    struct tm tmv;
    if (value == 0U) {
        printf("  %-16s 0\n", name);
        return;
    }
    if (localtime_r(&t, &tmv) != NULL && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z", &tmv) != 0U) 
        printf("  %-16s %u (%s)\n", name, value, buf);
    else printf("  %-16s %u\n", name, value);
}

static void print_block_pointer_summary(const ext2_inode_t *inode) {
    for (size_t i = 0; i < EXT2_NDIR_BLOCKS; i++) printf("  direct[%2zu]        %" PRIu32 "\n", i, inode->block[i]);
    printf("  single indirect   %" PRIu32 "\n", inode->block[EXT2_IND_BLOCK]);
    printf("  double indirect   %" PRIu32 "\n", inode->block[EXT2_DIND_BLOCK]);
    printf("  triple indirect   %" PRIu32 "\n", inode->block[EXT2_TIND_BLOCK]);
}

typedef struct run_state {
    uint64_t first_logical, last_logical;
    uint32_t first_phys, last_phys;
    int is_hole, valid;
} run_state_t;

static void flush_run(const run_state_t *run) {
    if (!run->valid) return;
    if (run->first_logical == run->last_logical) printf("  logical %-12" PRIu64 " -> ", run->first_logical);
    else printf("  logical %-6" PRIu64 "..%-6" PRIu64 " -> ", run->first_logical, run->last_logical);
    if (run->is_hole) puts("hole");
    else if (run->first_phys == run->last_phys) printf("physical %" PRIu32 "\n", run->first_phys);
    else printf("physical %" PRIu32 "..%" PRIu32 "\n", run->first_phys, run->last_phys);
}

static void print_data_block_map(const ext2_fs_t *fs, const ext2_inode_t *inode) {
    uint64_t size = ext2_inode_size(inode);
    uint64_t logical_blocks = (size + fs->block_size - 1U) / fs->block_size;
    run_state_t run;
    puts("\nLogical data block map, compressed:");
    if (logical_blocks == 0U) {
        puts("  empty file: no data blocks");
        return;
    }
    memset(&run, 0, sizeof(run));
    for (uint64_t i = 0; i < logical_blocks; i++) {
        uint32_t phys;
        int is_hole, continues;
        if (ext2_get_data_block(fs, inode, i, &phys) == -1) {
            fprintf(stderr, "%s: cannot read block map at logical block %" PRIu64 ": %s\n", program_name, i, strerror(errno));
            flush_run(&run);
            return;
        }
        is_hole = (phys == 0U);
        continues = run.valid && run.is_hole == is_hole && ((is_hole && i == run.last_logical + 1U) || (!is_hole && i == run.last_logical + 1U && phys == run.last_phys + 1U));
        if (continues) {
            run.last_logical = i;
            run.last_phys = phys;
        } else {
            flush_run(&run);
            run.first_logical = run.last_logical = i;
            run.first_phys = run.last_phys = phys;
            run.is_hole = is_hole;
            run.valid = 1;
        }
    }
    flush_run(&run);
}

int main(int argc, char **argv) {
    ext2_fs_t fs;
    ext2_inode_t inode;
    uint32_t ino, group, index;
    char modes[11];

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

    group = (ino - 1U) / fs.inodes_per_group;
    index = (ino - 1U) % fs.inodes_per_group;
    ext2_mode_string(inode.mode, modes);

    printf("Filesystem: %s\n", fs.path);
    printf("Host byte order: %s\n", EXT2_HOST_LITTLE_ENDIAN ? "little-endian" : "big-endian");
    printf("Superblock:\n");
    printf("  block size       %" PRIu32 "\n", fs.block_size);
    printf("  blocks count     %" PRIu32 "\n", fs.blocks_count);
    printf("  inodes count     %" PRIu32 "\n", fs.inodes_count);
    printf("  blocks/group     %" PRIu32 "\n", fs.blocks_per_group);
    printf("  inodes/group     %" PRIu32 "\n", fs.inodes_per_group);
    printf("  inode size       %" PRIu16 "\n", fs.inode_size);
    printf("  groups count     %" PRIu32 "\n", fs.group_count);
    printf("  feature incompat 0x%08" PRIx32 "\n", fs.feature_incompat);
    printf("  feature rocompat 0x%08" PRIx32 "\n", fs.feature_ro_compat);
    if (fs.volume_name[0] != '\0') printf("  volume name      %s\n", fs.volume_name);

    printf("\nInode #%" PRIu32 ":\n", ino);
    printf("  group/index      %" PRIu32 "/%" PRIu32 "\n", group, index);
    printf("  type             %s\n", ext2_file_type_name(inode.mode));
    printf("  mode             0%06o (%s)\n", inode.mode, modes);
    printf("  uid/gid          %" PRIu16 "/%" PRIu16 "\n", inode.uid, inode.gid);
    printf("  size             %" PRIu64 " bytes\n", ext2_inode_size(&inode));
    printf("  links            %" PRIu16 "\n", inode.links_count);
    printf("  blocks           %" PRIu32 " sectors of 512 bytes\n", inode.blocks_512);
    printf("  fs blocks approx %" PRIu32 "\n", inode.blocks_512 / (fs.block_size / 512U));
    printf("  flags            0x%08" PRIx32 "\n", inode.flags);
    printf("  generation       %" PRIu32 "\n", inode.generation);
    printf("  file_acl         %" PRIu32 "\n", inode.file_acl);
    printf("  dir_acl/size_hi  %" PRIu32 "\n", inode.dir_acl_or_size_high);
    printf("  faddr            %" PRIu32 "\n", inode.faddr);

    puts("\nTimes:");
    print_time_field("atime", inode.atime);
    print_time_field("ctime", inode.ctime);
    print_time_field("mtime", inode.mtime);
    print_time_field("dtime", inode.dtime);

    puts("\nRaw i_block pointers:");
    print_block_pointer_summary(&inode);
    print_data_block_map(&fs, &inode);

    ext2_close(&fs);
    return EXIT_SUCCESS;
}
