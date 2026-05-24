#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include "ext2.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

uint16_t ext2_le16_to_cpu(uint16_t x)
{
#if EXT2_HOST_LITTLE_ENDIAN
    return x;
#else
    return __builtin_bswap16(x);
#endif
}

uint32_t ext2_le32_to_cpu(uint32_t x)
{
#if EXT2_HOST_LITTLE_ENDIAN
    return x;
#else
    return __builtin_bswap32(x);
#endif
}

uint64_t ext2_le64_to_cpu(uint64_t x)
{
#if EXT2_HOST_LITTLE_ENDIAN
    return x;
#else
    return __builtin_bswap64(x);
#endif
}

uint16_t ext2_get_le16(const uint8_t *p)
{
    uint16_t x;
    memcpy(&x, p, sizeof(x));
    return ext2_le16_to_cpu(x);
}

uint32_t ext2_get_le32(const uint8_t *p)
{
    uint32_t x;
    memcpy(&x, p, sizeof(x));
    return ext2_le32_to_cpu(x);
}

uint64_t ext2_get_le64(const uint8_t *p)
{
    uint64_t x;
    memcpy(&x, p, sizeof(x));
    return ext2_le64_to_cpu(x);
}

void ext2_print_error(const char *program, const char *message)
{
    if (errno != 0) fprintf(stderr, "%s: %s: %s\n", program, message, strerror(errno));
    else fprintf(stderr, "%s: %s\n", program, message);
}

static uint32_t ceil_div_u32(uint32_t a, uint32_t b)
{
    return (a + b - 1U) / b;
}

static int parse_superblock(ext2_fs_t *fs, const ext2_superblock_disk_t *sb)
{
    uint16_t magic = ext2_le16_to_cpu(sb->magic);
    uint32_t groups_by_blocks;
    uint32_t groups_by_inodes;

    if (magic != EXT2_MAGIC) {
        errno = 0;
        return -1;
    }

    fs->inodes_count = ext2_le32_to_cpu(sb->inodes_count);
    fs->blocks_count = ext2_le32_to_cpu(sb->blocks_count);
    fs->first_data_block = ext2_le32_to_cpu(sb->first_data_block);
    fs->log_block_size = ext2_le32_to_cpu(sb->log_block_size);
    fs->blocks_per_group = ext2_le32_to_cpu(sb->blocks_per_group);
    fs->inodes_per_group = ext2_le32_to_cpu(sb->inodes_per_group);
    fs->rev_level = ext2_le32_to_cpu(sb->rev_level);
    fs->first_ino = 11;
    fs->inode_size = 128;
    fs->feature_compat = 0;
    fs->feature_incompat = 0;
    fs->feature_ro_compat = 0;
    memset(fs->volume_name, 0, sizeof(fs->volume_name));

    if (fs->rev_level >= 1U) {
        fs->first_ino = ext2_le32_to_cpu(sb->first_ino);
        fs->inode_size = ext2_le16_to_cpu(sb->inode_size);
        fs->feature_compat = ext2_le32_to_cpu(sb->feature_compat);
        fs->feature_incompat = ext2_le32_to_cpu(sb->feature_incompat);
        fs->feature_ro_compat = ext2_le32_to_cpu(sb->feature_ro_compat);
        memcpy(fs->volume_name, sb->volume_name, sizeof(sb->volume_name));
        fs->volume_name[16] = '\0';
    }

    if (fs->log_block_size > 16U || fs->blocks_per_group == 0U ||
        fs->inodes_per_group == 0U || fs->inode_size < 128U) {
        errno = 0;
        return -1;
    }

    fs->block_size = 1024U << fs->log_block_size;
    if (fs->block_size < 1024U || (fs->block_size % 1024U) != 0U) {
        errno = 0;
        return -1;
    }

    if ((fs->feature_incompat & ~EXT2_FEATURE_INCOMPAT_FILETYPE) != 0U) {
        errno = 0;
        return -1;
    }

    groups_by_blocks = ceil_div_u32(fs->blocks_count, fs->blocks_per_group);
    groups_by_inodes = ceil_div_u32(fs->inodes_count, fs->inodes_per_group);
    fs->group_count = groups_by_blocks > groups_by_inodes ? groups_by_blocks : groups_by_inodes;
    fs->gdt_offset = (fs->block_size == 1024U) ? 2048ULL : (uint64_t)fs->block_size;

    return 0;
}

int ext2_read_exact_at(const ext2_fs_t *fs, void *buf, size_t size, uint64_t offset)
{
    uint8_t *p = buf;
    size_t done = 0;

    while (done < size) {
        ssize_t r = pread(fs->fd, p + done, size - done, (off_t)(offset + done));
        if (r == 0) {
            errno = EIO;
            return -1;
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)r;
    }
    return 0;
}

int ext2_open(ext2_fs_t *fs, const char *path)
{
    ext2_superblock_disk_t sb;

    memset(fs, 0, sizeof(*fs));
    fs->fd = -1;
    fs->path = strdup(path);
    if (fs->path == NULL) return -1;

    fs->fd = open(path, O_RDONLY);
    if (fs->fd == -1) {
        free(fs->path);
        fs->path = NULL;
        return -1;
    }

    if (ext2_read_exact_at(fs, &sb, sizeof(sb), EXT2_SUPERBLOCK_OFFSET) == -1) {
        ext2_close(fs);
        return -1;
    }

    if (parse_superblock(fs, &sb) == -1) {
        ext2_close(fs);
        return -1;
    }

    return 0;
}

void ext2_close(ext2_fs_t *fs)
{
    if (fs->fd != -1) {
        (void)close(fs->fd);
        fs->fd = -1;
    }
    free(fs->path);
    fs->path = NULL;
}

int ext2_read_group_desc(const ext2_fs_t *fs, uint32_t group, ext2_group_desc_t *gd)
{
    ext2_group_desc_disk_t raw;
    uint64_t off;

    if (group >= fs->group_count) {
        errno = EINVAL;
        return -1;
    }

    off = fs->gdt_offset + (uint64_t)group * 32ULL;
    if (ext2_read_exact_at(fs, &raw, sizeof(raw), off) == -1) return -1;

    gd->block_bitmap = ext2_le32_to_cpu(raw.block_bitmap);
    gd->inode_bitmap = ext2_le32_to_cpu(raw.inode_bitmap);
    gd->inode_table = ext2_le32_to_cpu(raw.inode_table);
    gd->free_blocks_count = ext2_le16_to_cpu(raw.free_blocks_count);
    gd->free_inodes_count = ext2_le16_to_cpu(raw.free_inodes_count);
    gd->used_dirs_count = ext2_le16_to_cpu(raw.used_dirs_count);
    return 0;
}

int ext2_read_inode(const ext2_fs_t *fs, uint32_t ino, ext2_inode_t *inode)
{
    uint32_t group;
    uint32_t index;
    uint64_t inode_offset;
    ext2_inode_disk_t *raw;
    ext2_group_desc_t gd;
    size_t i;

    if (ino == 0U || ino > fs->inodes_count) {
        errno = EINVAL;
        return -1;
    }

    group = (ino - 1U) / fs->inodes_per_group;
    index = (ino - 1U) % fs->inodes_per_group;
    if (ext2_read_group_desc(fs, group, &gd) == -1) return -1;

    inode_offset = (uint64_t)gd.inode_table * fs->block_size + (uint64_t)index * fs->inode_size;

    raw = malloc(fs->inode_size);
    if (raw == NULL) return -1;

    if (ext2_read_exact_at(fs, raw, fs->inode_size, inode_offset) == -1) {
        free(raw);
        return -1;
    }

    memset(inode, 0, sizeof(*inode));
    inode->mode = ext2_le16_to_cpu(raw->mode);
    inode->uid = ext2_le16_to_cpu(raw->uid);
    inode->size_lo = ext2_le32_to_cpu(raw->size_lo);
    inode->atime = ext2_le32_to_cpu(raw->atime);
    inode->ctime = ext2_le32_to_cpu(raw->ctime);
    inode->mtime = ext2_le32_to_cpu(raw->mtime);
    inode->dtime = ext2_le32_to_cpu(raw->dtime);
    inode->gid = ext2_le16_to_cpu(raw->gid);
    inode->links_count = ext2_le16_to_cpu(raw->links_count);
    inode->blocks_512 = ext2_le32_to_cpu(raw->blocks_512);
    inode->flags = ext2_le32_to_cpu(raw->flags);
    inode->osd1 = ext2_le32_to_cpu(raw->osd1);
    memcpy(inode->raw_i_block, raw->i_block, sizeof(inode->raw_i_block));
    for (i = 0; i < EXT2_N_BLOCKS; i++)
        inode->block[i] = ext2_get_le32(raw->i_block + i * 4U);
    inode->generation = ext2_le32_to_cpu(raw->generation);
    inode->file_acl = ext2_le32_to_cpu(raw->file_acl);
    inode->dir_acl_or_size_high = ext2_le32_to_cpu(raw->dir_acl_or_size_high);
    inode->faddr = ext2_le32_to_cpu(raw->faddr);

    free(raw);
    return 0;
}

int ext2_inode_is_regular(const ext2_inode_t *inode)
{
    return (inode->mode & EXT2_S_IFMT) == EXT2_S_IFREG;
}

int ext2_inode_is_directory(const ext2_inode_t *inode)
{
    return (inode->mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

int ext2_inode_is_symlink(const ext2_inode_t *inode)
{
    return (inode->mode & EXT2_S_IFMT) == EXT2_S_IFLNK;
}

uint64_t ext2_inode_size(const ext2_inode_t *inode)
{
    uint64_t size = inode->size_lo;
    if (ext2_inode_is_regular(inode))
        size |= (uint64_t)inode->dir_acl_or_size_high << 32;
    return size;
}

const char *ext2_file_type_name(uint16_t mode)
{
    switch (mode & EXT2_S_IFMT) {
        case EXT2_S_IFREG: return "regular file";
        case EXT2_S_IFDIR: return "directory";
        case EXT2_S_IFLNK: return "symbolic link";
        case EXT2_S_IFCHR: return "character device";
        case EXT2_S_IFBLK: return "block device";
        case EXT2_S_IFIFO: return "fifo";
        case EXT2_S_IFSOCK: return "socket";
        default: return "unknown";
    }
}

void ext2_mode_string(uint16_t mode, char out[11])
{
    out[0] = '?';
    switch (mode & EXT2_S_IFMT) {
        case EXT2_S_IFREG:  out[0] = '-'; break;
        case EXT2_S_IFDIR:  out[0] = 'd'; break;
        case EXT2_S_IFLNK:  out[0] = 'l'; break;
        case EXT2_S_IFCHR:  out[0] = 'c'; break;
        case EXT2_S_IFBLK:  out[0] = 'b'; break;
        case EXT2_S_IFIFO:  out[0] = 'p'; break;
        case EXT2_S_IFSOCK: out[0] = 's'; break;
        default:            out[0] = '?'; break;
    }
    out[1] = (mode & 0400U) ? 'r' : '-';
    out[2] = (mode & 0200U) ? 'w' : '-';
    out[3] = (mode & 0100U) ? 'x' : '-';
    out[4] = (mode & 0040U) ? 'r' : '-';
    out[5] = (mode & 0020U) ? 'w' : '-';
    out[6] = (mode & 0010U) ? 'x' : '-';
    out[7] = (mode & 0004U) ? 'r' : '-';
    out[8] = (mode & 0002U) ? 'w' : '-';
    out[9] = (mode & 0001U) ? 'x' : '-';
    if (mode & 04000U) out[3] = (out[3] == 'x') ? 's' : 'S';
    if (mode & 02000U) out[6] = (out[6] == 'x') ? 's' : 'S';
    if (mode & 01000U) out[9] = (out[9] == 'x') ? 't' : 'T';
    out[10] = '\0';
}

const char *ext2_dirent_type_name(uint8_t type)
{
    switch (type) {
        case 0: return "unknown";
        case 1: return "regular file";
        case 2: return "directory";
        case 3: return "character device";
        case 4: return "block device";
        case 5: return "fifo";
        case 6: return "socket";
        case 7: return "symbolic link";
        default: return "invalid";
    }
}

static int read_u32_from_block(const ext2_fs_t *fs, uint32_t block_no, uint64_t index, uint32_t *value)
{
    uint8_t raw[4];
    uint64_t off;

    if (block_no == 0U) {
        *value = 0;
        return 0;
    }
    if (index >= fs->block_size / 4U) {
        errno = EINVAL;
        return -1;
    }

    off = (uint64_t)block_no * fs->block_size + index * 4ULL;
    if (ext2_read_exact_at(fs, raw, sizeof(raw), off) == -1) return -1;
    *value = ext2_get_le32(raw);
    return 0;
}

int ext2_get_data_block(const ext2_fs_t *fs, const ext2_inode_t *inode, uint64_t logical_block, uint32_t *physical_block)
{
    uint64_t n = fs->block_size / 4U;
    uint64_t l = logical_block;
    uint32_t p1;
    uint32_t p2;

    if (l < EXT2_NDIR_BLOCKS) {
        *physical_block = inode->block[l];
        return 0;
    }
    l -= EXT2_NDIR_BLOCKS;

    if (l < n) return read_u32_from_block(fs, inode->block[EXT2_IND_BLOCK], l, physical_block);
    l -= n;

    if (l < n * n) {
        if (read_u32_from_block(fs, inode->block[EXT2_DIND_BLOCK], l / n, &p1) == -1) return -1;
        return read_u32_from_block(fs, p1, l % n, physical_block);
    }
    l -= n * n;

    if (l < n * n * n) {
        if (read_u32_from_block(fs, inode->block[EXT2_TIND_BLOCK], l / (n * n), &p1) == -1) return -1;
        if (read_u32_from_block(fs, p1, (l / n) % n, &p2) == -1) return -1;
        return read_u32_from_block(fs, p2, l % n, physical_block);
    }

    errno = EFBIG;
    return -1;
}

static int write_all(int fd, const void *buf, size_t size)
{
    const uint8_t *p = buf;
    size_t done = 0;

    while (done < size) {
        ssize_t w = write(fd, p + done, size - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) {
            errno = EIO;
            return -1;
        }
        done += (size_t)w;
    }
    return 0;
}

static int write_zeroes(int fd, uint64_t count)
{
    uint8_t zeroes[8192];
    memset(zeroes, 0, sizeof(zeroes));

    while (count > 0U) {
        size_t chunk = count > sizeof(zeroes) ? sizeof(zeroes) : (size_t)count;
        if (write_all(fd, zeroes, chunk) == -1) return -1;
        count -= chunk;
    }
    return 0;
}

int ext2_write_inode_data_to_fd(const ext2_fs_t *fs, const ext2_inode_t *inode, int out_fd)
{
    uint64_t size = ext2_inode_size(inode);
    uint64_t logical_blocks;
    uint64_t i;
    uint8_t *buf;

    if (ext2_inode_is_symlink(inode) && inode->blocks_512 == 0U && size <= sizeof(inode->raw_i_block))
        return write_all(out_fd, inode->raw_i_block, (size_t)size);

    logical_blocks = (size + fs->block_size - 1U) / fs->block_size;
    buf = malloc(fs->block_size);
    if (buf == NULL) return -1;

    for (i = 0; i < logical_blocks; i++) {
        uint32_t phys;
        uint64_t remaining = size - i * (uint64_t)fs->block_size;
        size_t chunk = remaining > fs->block_size ? fs->block_size : (size_t)remaining;

        if (ext2_get_data_block(fs, inode, i, &phys) == -1) {
            free(buf);
            return -1;
        }

        if (phys == 0U) {
            if (write_zeroes(out_fd, chunk) == -1) {
                free(buf);
                return -1;
            }
        } else {
            uint64_t off = (uint64_t)phys * fs->block_size;
            if (ext2_read_exact_at(fs, buf, chunk, off) == -1) {
                free(buf);
                return -1;
            }
            if (write_all(out_fd, buf, chunk) == -1) {
                free(buf);
                return -1;
            }
        }
    }

    free(buf);
    return 0;
}
