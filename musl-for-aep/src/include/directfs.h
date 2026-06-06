#ifndef	_DIRECTFS_H
#define	_DIRECTFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_off_t
#define __NEED_pid_t
#define __NEED_mode_t

#ifdef _GNU_SOURCE
#define __NEED_size_t
#define __NEED_ssize_t
#define __NEED_struct_iovec
#endif

#include <bits/alltypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lock.h>
#include "syscall.h"

#define TABLE_SIZE 128 // 哈希映射表大小，根据实际需要调整
#define PAGE_SHIFT 12 
#define PAGE_SIZE  (1 << PAGE_SHIFT)
#define HLIST_HSHIFT 24
#define LIST_BL_LOCKMASK	0UL
#define PRIVATE_VMALLOC_START 0xffffe88000000000
#define PRIVATE_VMALLOC_END PRIVATE_VMALLOC_START + 0x0000008000000000

#define max_log_index 66666

#define max_file_per_process 64
#define max_file_per_app 256

#define SYS_DIRECTFS_ALLOC_PAGES 451
#define SYS_DIRECTFS_READ  453
#define SYS_DIRECTFS_WRITE 454
#define SYS_DIRECTFS_FLUSH_LOGS  455 

#define PAGE_ALIGN_UP(addr) ((addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(addr) (addr & ~(PAGE_SIZE - 1))
#define OFFSET_TO_INDEX(offset) (offset >> PAGE_SHIFT)
#define INDEX_TO_OFFSET(index) (index << PAGE_SHIFT)

// Used to determine contents of flags passed to OPEN
#define FLAGS_INCLUDE(flags, x) ((flags&x)||(x==0))
#define DO_ALIGNMENT_CHECKS 0

#define MIN_DIRECT_FD (1UL << 32)
#define G_FD_MASK 0xffffffff00000000
#define FD_MASK (~G_FD_MASK)

#define MIN(a,b) ((a)<(b) ? (a) : (b))

#define print_debug 0

#define DEBUG_PRINT(fmt, ...) \
    do { \
        if (print_debug) { \
            printf(fmt, ##__VA_ARGS__); \
        } \
    } while (0)

#define is_pv_addr(addr) (((unsigned long)addr >= PRIVATE_VMALLOC_START) && ((unsigned long)addr <= PRIVATE_VMALLOC_END))

#define is_legal_fd(fd) ((fd >= 0) && (fd <= max_file_per_process))	
#define is_legal_group_fd(fd) ((fd > 0) && (fd <= max_file_per_app))

#define IS_DIRECT_FILE(file) ((file->group_fd > 0) && (file->node != NULL))

#define node_lock(lock) LOCK(&lock)

#define node_unlock(lock) UNLOCK(&lock)

struct DirectFSPage {
	struct DirectFSPage* seq_next;
	unsigned long addr;
	size_t length;
	off_t index; // offset in file
};

struct DirectNode {
	volatile int lock;
	int group_fd;
	unsigned long length; // length of the file
	unsigned long true_length; // length of the file include appended data
	int count; // reference count of the file
	struct DirectFSPage* seq_page_list;
	struct DirectNode* next;
};

extern int __log_lock[1];
struct log_entry {
	unsigned long groupfd;
	off_t offset; // write or append data offset in file
	size_t count; // write or append data size
	bool is_new_alloc;
	unsigned long addr; // append data address in pv
	unsigned long struct_file_addr; // for kernel file struct
};

struct user_template_metadata {
	unsigned long user_pages_start_addr;
	unsigned long user_pages_end_addr; // alloc page from user_pages_end_addr to user_pages_start_addr
	unsigned long log_index;
};

extern unsigned long *groupfd_addr;
extern unsigned long *groupfd_size;
extern unsigned char *pv_bit_maps;
extern struct log_entry *log_entrys;

extern int trap_count;

extern struct DirectNode* hashTable[TABLE_SIZE];

int hash(int fd);

void open_direct_pre_handle(unsigned long _fd, int flags, int *fd, int *group_fd);

void put_DirectNode(int group_fd);

struct DirectNode* get_DirectNode(int group_fd, int flags);

struct DirectNode* find_DirectNode(int fd); 

struct DirectNode* find_file_node(int group_fd);

void free_DirectFile(int fd);

void ref_add(struct DirectNode* node);

int ref_dec(struct DirectNode* node);

int test_file_in_pv(struct DirectNode* node, off_t offset, size_t count);

int pv_test_bit(unsigned long addr);

int ra_test_bit(unsigned long addr);

void append_log_entry(unsigned long groupfd, off_t offset, size_t count, 
					bool is_new_alloc, unsigned long addr, unsigned long struct_file_addr);

unsigned long alloc_pages(size_t count);

#endif
