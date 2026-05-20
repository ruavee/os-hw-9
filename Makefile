CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -std=c11 -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64
LDFLAGS ?=
VALGRIND ?= valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=99

TOOLS = ext2_inode_info ext2_cat_inode ext2_dir_entries
COMMON_OBJ = ext2.o

.PHONY: all test valgrind-test clean distclean

all: $(TOOLS)

ext2_inode_info: inode_info.o $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ext2_cat_inode: inode_data.o $(COMMON_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ext2_dir_entries: dir_entries.o ext2.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c ext2.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	./test_ext2.sh

valgrind-test: all
	RUN_VALGRIND=1 VALGRIND_CMD='$(VALGRIND)' ./test_ext2.sh

clean:
	rm -f *.o $(TOOLS)

distclean: clean
	rm -rf test-work ext2.img ext2
