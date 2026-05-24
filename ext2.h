#ifndef EXT2_H
#define EXT2_H

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#if defined(__GNUC__) || defined(__clang__)
#define EXT2_PACKED __attribute__((__packed__))
#else
#define EXT2_PACKED
#endif

#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__) || !defined(__ORDER_BIG_ENDIAN__)
#error "Cannot determine host byte order at compile time"
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define EXT2_HOST_LITTLE_ENDIAN 1
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define EXT2_HOST_LITTLE_ENDIAN 0
#else
#error "Unsupported host byte order"
#endif

#define EXT2_SUPERBLOCK_OFFSET 1024ULL
#define EXT2_SUPERBLOCK_SIZE   1024U
#define EXT2_MAGIC             0xEF53U

#define EXT2_NDIR_BLOCKS 12U
#define EXT2_IND_BLOCK   12U
#define EXT2_DIND_BLOCK  13U
#define EXT2_TIND_BLOCK  14U
#define EXT2_N_BLOCKS    15U

#define EXT2_S_IFSOCK 0xC000U
#define EXT2_S_IFLNK  0xA000U
#define EXT2_S_IFREG  0x8000U
#define EXT2_S_IFBLK  0x6000U
#define EXT2_S_IFDIR  0x4000U
#define EXT2_S_IFCHR  0x2000U
#define EXT2_S_IFIFO  0x1000U
#define EXT2_S_IFMT   0xF000U

#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002U
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE 0x0002U

typedef struct EXT2_PACKED ext2_superblock_disk {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t reserved_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t log_frag_size;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t max_mnt_count;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_rev_level;
    uint32_t lastcheck;
    uint32_t checkinterval;
    uint32_t creator_os;
    uint32_t rev_level;
    uint16_t def_resuid;
    uint16_t def_resgid;
    uint32_t first_ino;
    uint16_t inode_size;
    uint16_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint8_t uuid[16];
    char volume_name[16];
    uint8_t unused[888];
} ext2_superblock_disk_t;

typedef struct EXT2_PACKED ext2_group_desc_disk {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t pad;
    uint32_t reserved[3];
} ext2_group_desc_disk_t;

typedef struct EXT2_PACKED ext2_inode_disk {
    uint16_t mode;
    uint16_t uid;
    uint32_t size_lo;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks_512;
    uint32_t flags;
    uint32_t osd1;
    uint8_t i_block[60];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t dir_acl_or_size_high;
    uint32_t faddr;
    uint8_t osd2[12];
} ext2_inode_disk_t;

_Static_assert(sizeof(ext2_superblock_disk_t) == EXT2_SUPERBLOCK_SIZE, "unexpected ext2 superblock layout");
_Static_assert(sizeof(ext2_group_desc_disk_t) == 32U, "unexpected ext2 group descriptor layout");
_Static_assert(sizeof(ext2_inode_disk_t) == 128U, "unexpected ext2 inode layout");

typedef struct ext2_fs {
    int fd;
    char *path;

    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t rev_level;
    uint32_t first_ino;
    uint16_t inode_size;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint32_t block_size;
    uint32_t group_count;
    uint64_t gdt_offset;
    char volume_name[17];
} ext2_fs_t;

typedef struct ext2_group_desc {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
} ext2_group_desc_t;

typedef struct ext2_inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size_lo;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks_512;
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[EXT2_N_BLOCKS];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t dir_acl_or_size_high;
    uint32_t faddr;
    uint8_t raw_i_block[60];
} ext2_inode_t;

uint16_t ext2_le16_to_cpu(uint16_t x);
uint32_t ext2_le32_to_cpu(uint32_t x);
uint64_t ext2_le64_to_cpu(uint64_t x);
uint16_t ext2_get_le16(const uint8_t *p);
uint32_t ext2_get_le32(const uint8_t *p);
uint64_t ext2_get_le64(const uint8_t *p);

int ext2_open(ext2_fs_t *fs, const char *path);
void ext2_close(ext2_fs_t *fs);
int ext2_read_exact_at(const ext2_fs_t *fs, void *buf, size_t size, uint64_t offset);
int ext2_read_group_desc(const ext2_fs_t *fs, uint32_t group, ext2_group_desc_t *gd);
int ext2_read_inode(const ext2_fs_t *fs, uint32_t ino, ext2_inode_t *inode);
uint64_t ext2_inode_size(const ext2_inode_t *inode);
int ext2_inode_is_regular(const ext2_inode_t *inode);
int ext2_inode_is_directory(const ext2_inode_t *inode);
int ext2_inode_is_symlink(const ext2_inode_t *inode);
const char *ext2_file_type_name(uint16_t mode);
void ext2_mode_string(uint16_t mode, char out[11]);
const char *ext2_dirent_type_name(uint8_t type);
int ext2_get_data_block(const ext2_fs_t *fs, const ext2_inode_t *inode,
                        uint64_t logical_block, uint32_t *physical_block);
int ext2_write_inode_data_to_fd(const ext2_fs_t *fs, const ext2_inode_t *inode, int out_fd);
void ext2_print_error(const char *program, const char *message);

#endif
