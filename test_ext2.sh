#!/usr/bin/env bash
set -euo pipefail

IMAGE=${IMAGE:-ext2.img}
MNT=${MNT:-ext2}
WORK=${WORK:-test-work}
SIZE=${SIZE:-192M}
BLOCK_SIZE=${BLOCK_SIZE:-2048}
RUN_VALGRIND=${RUN_VALGRIND:-0}
HASH_LARGE_SPARSE=${HASH_LARGE_SPARSE:-0}
VALGRIND_CMD=${VALGRIND_CMD:-valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=99}

if [[ "$RUN_VALGRIND" == "1" ]]; then
  read -r -a VG <<< "$VALGRIND_CMD"
  if ! command -v "${VG[0]}" >/dev/null 2>&1; then
    printf 'error: %s not found; install valgrind or run plain make test\n' "${VG[0]}" >&2
    exit 127
  fi
else
  VG=()
fi

mkdir -p "$WORK" "$MNT"
truncate --size "$SIZE" "$IMAGE"
mkfs.ext2 -F -b "$BLOCK_SIZE" "$IMAGE" > "$WORK/mkfs.log"

mounted=0
loopdev=""
cleanup() {
  if [[ "$mounted" == "1" ]]; then
    sudo umount "$MNT" || true
  fi
  if [[ -n "$loopdev" ]]; then
    sudo losetup -d "$loopdev" || true
  fi
}
trap cleanup EXIT

sudo mount -t ext2 -o loop "$IMAGE" "$MNT"
mounted=1
sudo chown "$(id -u):$(id -g)" "$MNT"

mkdir -p "$MNT/dir_a/dir_b" "$MNT/dir_c"
printf 'hello from ext2 homework\n' > "$MNT/small.txt"
python3 - <<'PY' > "$MNT/indirect.bin"
import os, sys
# More than 12 filesystem blocks for 2K/4K block sizes, so indirect addressing is used.
sys.stdout.buffer.write(os.urandom(256 * 1024))
PY
truncate -s 16M "$MNT/sparse_checksum.bin"
printf 'BEGIN-SPARSE-CHECKSUM\n' | dd of="$MNT/sparse_checksum.bin" bs=1 seek=0 conv=notrunc status=none
printf 'MIDDLE-SPARSE-CHECKSUM\n' | dd of="$MNT/sparse_checksum.bin" bs=1 seek=$((8*1024*1024 + 123)) conv=notrunc status=none
printf 'END-SPARSE-CHECKSUM\n' | dd of="$MNT/sparse_checksum.bin" bs=1 seek=$((16*1024*1024 - 4096)) conv=notrunc status=none
truncate -s 5G "$MNT/sparse_5g.bin"
printf 'BEGIN-SPARSE\n' | dd of="$MNT/sparse_5g.bin" bs=1 seek=0 conv=notrunc status=none
printf 'END-SPARSE\n' | dd of="$MNT/sparse_5g.bin" bs=1 seek=$((5*1024*1024*1024 - 4096)) conv=notrunc status=none
printf 'nested file\n' > "$MNT/dir_a/dir_b/nested.txt"
printf 'another file\n' > "$MNT/dir_c/another.txt"
ln -s small.txt "$MNT/link_to_small"
sync

{
  printf 'path\tinode\ttype\tsha512\n'
  for p in small.txt indirect.bin sparse_checksum.bin dir_a dir_a/dir_b dir_c link_to_small dir_a/dir_b/nested.txt dir_c/another.txt; do
    inode=$(stat -c '%i' "$MNT/$p")
    type=$(stat -c '%F' "$MNT/$p")
    if [[ -f "$MNT/$p" ]]; then
      hash=$(sha512sum "$MNT/$p" | awk '{print $1}')
    else
      hash='-'
    fi
    printf '%s\t%s\t%s\t%s\n' "$p" "$inode" "$type" "$hash"
  done
  inode=$(stat -c '%i' "$MNT/sparse_5g.bin")
  if [[ "$HASH_LARGE_SPARSE" == "1" ]]; then
    hash=$(sha512sum "$MNT/sparse_5g.bin" | awk '{print $1}')
  else
    hash='skipped-set-HASH_LARGE_SPARSE=1-for-full-check'
  fi
  printf 'sparse_5g.bin\t%s\tregular file\t%s\n' "$inode" "$hash"
} > "$WORK/inodes.tsv"

sudo umount "$MNT"
mounted=0

get_ino() { awk -F '\t' -v p="$1" 'NR > 1 && $1 == p {print $2}' "$WORK/inodes.tsv"; }
get_hash() { awk -F '\t' -v p="$1" 'NR > 1 && $1 == p {print $4}' "$WORK/inodes.tsv"; }

for p in small.txt indirect.bin sparse_checksum.bin dir_a/dir_b/nested.txt dir_c/another.txt; do
  ino=$(get_ino "$p")
  "${VG[@]}" ./ext2_inode_info "$IMAGE" "$ino" > "$WORK/info-${p//\//_}.txt"
  "${VG[@]}" ./ext2_cat_inode "$IMAGE" "$ino" > "$WORK/out-${p//\//_}"
  got=$(sha512sum "$WORK/out-${p//\//_}" | awk '{print $1}')
  exp=$(get_hash "$p")
  [[ "$got" == "$exp" ]]
done

sparse_ino=$(get_ino sparse_5g.bin)
"${VG[@]}" ./ext2_inode_info "$IMAGE" "$sparse_ino" > "$WORK/info-sparse_5g.txt"
if [[ "$HASH_LARGE_SPARSE" == "1" ]]; then
  got=$("${VG[@]}" ./ext2_cat_inode "$IMAGE" "$sparse_ino" | sha512sum | awk '{print $1}')
  exp=$(get_hash sparse_5g.bin)
  [[ "$got" == "$exp" ]]
else
  # Smoke-test without streaming 5 GiB by checking metadata only.
  grep -q 'size             5368709120 bytes' "$WORK/info-sparse_5g.txt"
fi

for d in dir_a dir_a/dir_b dir_c; do
  ino=$(get_ino "$d")
  "${VG[@]}" ./ext2_cat_inode "$IMAGE" "$ino" > "$WORK/${d//\//_}.dirbin"
  "${VG[@]}" ./ext2_dir_entries "$WORK/${d//\//_}.dirbin" > "$WORK/${d//\//_}.dirlist"
done

grep -q 'name="dir_b"' "$WORK/dir_a.dirlist"
grep -q 'name="nested.txt"' "$WORK/dir_a_dir_b.dirlist"
grep -q 'name="another.txt"' "$WORK/dir_c.dirlist"

loopdev=$(sudo losetup -f)
sudo losetup "$loopdev" "$IMAGE"
sudo chmod a+r "$loopdev"
small_ino=$(get_ino small.txt)
./ext2_inode_info "$loopdev" "$small_ino" > "$WORK/info-loop-small.txt"
./ext2_cat_inode "$loopdev" "$small_ino" | sha512sum > "$WORK/loop-small.sha512"
loop_hash=$(awk '{print $1}' "$WORK/loop-small.sha512")
[[ "$loop_hash" == "$(get_hash small.txt)" ]]
sudo losetup -d "$loopdev"
loopdev=""

printf 'OK: ext2 homework test passed. Generated files are kept in %s and image is %s\n' "$WORK" "$IMAGE"
